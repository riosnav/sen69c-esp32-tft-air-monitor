// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The web server's handlers: the pages, the JSON API, the web
/// password and the setup hotspot's captive portal (see Web.h).

#include "Web.h"

#include <Arduino.h>
#include <Ipv4.h>
#include <LoginGuard.h>
#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

#include "AppLock.h"
#include "Device.h"
#include "System.h"
#include "Uptime.h"
#include "net/Json.h"
#include "ui/Ui.h"
#include "web/WebAssets.h"

using aq::Level;
using aq::Metric;

namespace web {

namespace {

constexpr size_t kMaxBodyBytes = 2048;   ///< The largest JSON request body, in bytes.
constexpr size_t kOtaChunkBytes = 4096;  ///< How much of an image is received at a time, in bytes.
constexpr int kRecvRetries = 5;  ///< How often receiving may time out before a request fails.

Context ctx;                      ///< What the handlers reach (see begin()).
httpd_handle_t server = nullptr;  ///< The running server; nullptr before begin().
/// @brief Slows down guessing of the web password.
///
/// Handlers all run in the server's task, so the guard needs no lock; the
/// count is copied out for the UI.
net::LoginGuard guard;
std::atomic<uint32_t> loginFailures{0};  ///< Wrong web passwords since start, for the UI.

// ---------------------------------------------------------------- helpers

/// @brief A level's ID in the JSON.
/// @param level The level.
/// @return "good", "warning", "alarm" or "unknown".
const char* levelId(Level level) {
  switch (level) {
    case Level::Good:
      return "good";
    case Level::Warning:
      return "warning";
    case Level::Alarm:
      return "alarm";
    default:
      return "unknown";
  }
}

/// @brief The sensor's state's ID in the JSON.
/// @param state The state.
/// @return "measuring", "busy", "not found" or "starting".
const char* sensorStateId(SensorState state) {
  switch (state) {
    case SensorState::Measuring:
      return "measuring";
    case SensorState::Busy:
      return "busy";
    case SensorState::NotFound:
      return "not found";
    default:
      return "starting";
  }
}

/// @brief Wi-Fi's state's ID in the JSON.
/// @param state The state.
/// @return "searching", "connecting", "connected" or "off".
const char* wifiStateId(WifiState state) {
  switch (state) {
    case WifiState::Searching:
      return "searching";
    case WifiState::Connecting:
      return "connecting";
    case WifiState::Connected:
      return "connected";
    default:
      return "off";
  }
}

/// @brief The setup hotspot's state's ID in the JSON.
/// @param state The state.
/// @return "waiting", "joined", "testing", "succeeded", "failed" or "off".
const char* setupStateId(SetupState state) {
  switch (state) {
    case SetupState::Waiting:
      return "waiting";
    case SetupState::Joined:
      return "joined";
    case SetupState::Testing:
      return "testing";
    case SetupState::Succeeded:
      return "succeeded";
    case SetupState::Failed:
      return "failed";
    default:
      return "off";
  }
}

/// @brief The battery's state's ID in the JSON.
/// @param state The state.
/// @return "charging", "discharging", "idle" or "unknown".
const char* batteryStateId(power::BatteryState state) {
  switch (state) {
    case power::BatteryState::Charging:
      return "charging";
    case power::BatteryState::Discharging:
      return "discharging";
    case power::BatteryState::Idle:
      return "idle";
    default:
      return "unknown";
  }
}

/// @brief An update's state's ID in the JSON.
/// @param state The state.
/// @return "receiving", "confirming", "accepted", "refused", "timedOut",
///   "failed" or "idle".
const char* otaStateId(OtaState state) {
  switch (state) {
    case OtaState::Receiving:
      return "receiving";
    case OtaState::Confirming:
      return "confirming";
    case OtaState::Accepted:
      return "accepted";
    case OtaState::Refused:
      return "refused";
    case OtaState::TimedOut:
      return "timedOut";
    case OtaState::Failed:
      return "failed";
    default:
      return "idle";
  }
}

/// @brief Why the monitor last started, as an ID in the JSON, which tells a
/// crash from a power cut.
/// @param reason From esp_reset_reason().
/// @return E.g. "power-on", "crash" or "watchdog"; "other" for the rest.
const char* resetReasonId(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";  // also the Reset button
    case ESP_RST_SW:
      return "restart";  // the menu, the web page, an update
    case ESP_RST_PANIC:
      return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_DEEPSLEEP:
      return "wake";  // after Power off or an empty battery
    case ESP_RST_USB:
      return "usb";  // flashed over USB
    default:
      return "other";
  }
}

/// @brief The MQTT client's state's ID in the JSON.
/// @param state The state.
/// @return "waiting", "connecting", "connected", "failed" or "off".
const char* mqttStateId(MqttState state) {
  switch (state) {
    case MqttState::Waiting:
      return "waiting";
    case MqttState::Connecting:
      return "connecting";
    case MqttState::Connected:
      return "connected";
    case MqttState::Failed:
      return "failed";
    default:
      return "off";
  }
}

/// @brief Adds the MQTT client's state and error to a JSON object.
/// @param object The object.
/// @param mqtt The client.
void addMqttState(cJSON* object, const MqttService& mqtt) {
  cJSON_AddStringToObject(object, "state", mqttStateId(mqtt.state()));
  cJSON_AddStringToObject(object, "error", mqtt.error());
}

/// @brief The status LED setting's ID in the JSON.
/// @param mode The setting.
/// @return "off", "dim" or "bright".
const char* ledId(LedMode mode) {
  return mode == LedMode::Off ? "off" : mode == LedMode::Dim ? "dim" : "bright";
}

/// @brief The Silence setting's ID in the JSON.
/// @param silence The setting.
/// @return "off", "warnings" or "all".
const char* silenceId(Silence silence) {
  return silence == Silence::Off ? "off" : silence == Silence::Warnings ? "warnings" : "all";
}

/// @brief Finds a metric by its ID in the JSON, e.g. "co2".
/// @param id The ID.
/// @param[out] out The metric, if found.
/// @return Whether it was.
bool metricFromId(const char* id, Metric& out) {
  for (size_t i = 0; i < aq::kMetricCount; ++i) {
    if (strcmp(id, aq::kMetricIds[i]) == 0) {
      out = static_cast<Metric>(i);
      return true;
    }
  }
  return false;
}

/// @brief A metric's limits as a JSON array, `[low, high]`, null where a side
/// has none.
/// @param low The low limit; NaN for none.
/// @param high The high limit; NaN for none.
/// @return The array, with the limits rounded to 1 decimal.
cJSON* limits(float low, float high) {
  cJSON* array = cJSON_CreateArray();
  for (float v : {low, high}) {
    cJSON_AddItemToArray(array, std::isnan(v) ? cJSON_CreateNull() : cJSON_CreateNumber(rounded(v, 1)));
  }
  return array;
}

/// @brief A member of a JSON object.
/// @param object The object.
/// @param key The member's key; case matters.
/// @return The member, or nullptr if there's none.
const cJSON* field(const cJSON* object, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(object, key);
}

/// @brief A text member of a JSON object.
/// @param object The object.
/// @param key The member's key; case matters.
/// @return Its text, or nullptr if it's missing or not text.
const char* stringField(const cJSON* object, const char* key) {
  const cJSON* f = field(object, key);
  return cJSON_IsString(f) ? f->valuestring : nullptr;
}

/// @brief Answers with JSON, not to be cached.
/// @param req The request.
/// @param json What to send.
/// @return ESP_OK, or an error to close the connection.
esp_err_t sendJson(httpd_req_t* req, const cJSON* json) {
  const JsonText text(cJSON_PrintUnformatted(json));
  if (!text) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, text.get());
}

/// @brief Answers with plain text.
/// @param req The request.
/// @param status The HTTP status, e.g. "400 Bad Request".
/// @param text The text.
/// @return ESP_OK, or an error to close the connection.
esp_err_t sendText(httpd_req_t* req, const char* status, const char* text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(req, text);
}

/// @brief Answers 400 Bad Request, saying why.
/// @param req The request.
/// @param reason What's wrong, for people.
/// @return ESP_OK, or an error to close the connection.
esp_err_t badRequest(httpd_req_t* req, const char* reason) {
  return sendText(req, "400 Bad Request", reason);
}

/// @brief Whether a request comes from the monitor's own pages (or a script
/// that says so), which mark theirs with an `X-Requested-With` header.
///
/// Another web site can make a visitor's browser send a form to the monitor,
/// with the browser's saved password, but it can't add a header like this
/// without asking the monitor first, which it never allows. So requests that
/// change something must have it.
/// @param req The request.
bool fromPage(httpd_req_t* req) { return httpd_req_get_hdr_value_len(req, "X-Requested-With") > 0; }

/// @brief Answers 403 to a request that changes something without the
/// `X-Requested-With` header (see fromPage()).
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t notFromPage(httpd_req_t* req) {
  return sendText(req, "403 Forbidden", "Requests that change something need an X-Requested-With header.");
}

/// @brief Answers 204 No Content: done.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t sendOk(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, nullptr, 0);
}

/// @brief Answers with a page that the firmware holds gzipped (see
/// `web/WebAssets.h`).
/// @param req The request.
/// @param asset The page.
/// @return ESP_OK, or an error to close the connection.
esp_err_t sendAsset(httpd_req_t* req, const WebAsset& asset) {
  httpd_resp_set_type(req, asset.type);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, reinterpret_cast<const char*>(asset.data), asset.size);
}

/// @brief Reads a JSON request body.
/// @param req The request.
/// @return The JSON; null if the body is empty, longer than kMaxBodyBytes,
///   cut short or not JSON.
Json readJson(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > kMaxBodyBytes) return nullptr;
  std::unique_ptr<char[]> body(new (std::nothrow) char[req->content_len + 1]);
  if (!body) return nullptr;
  size_t received = 0;
  int retries = 0;
  while (received < req->content_len) {
    const int n = httpd_req_recv(req, body.get() + received, req->content_len - received);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= kRecvRetries) continue;
    if (n <= 0) return nullptr;
    received += n;
  }
  body[received] = '\0';
  return Json(cJSON_ParseWithLength(body.get(), received));
}

/// @brief The local address the request came in on.
///
/// The server listens on IPv6, where IPv4 clients show up as IPv4-mapped
/// addresses.
/// @param req The request.
/// @return The IPv4 address; 0.0.0.0 if there's none.
IPAddress localAddress(httpd_req_t* req) {
  sockaddr_storage address{};
  socklen_t length = sizeof address;
  if (getsockname(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return IPAddress();
  }
  if (address.ss_family == AF_INET) {
    const auto* v4 = reinterpret_cast<const sockaddr_in*>(&address);
    const auto* b = reinterpret_cast<const uint8_t*>(&v4->sin_addr.s_addr);
    return IPAddress(b[0], b[1], b[2], b[3]);
  }
  if (address.ss_family == AF_INET6) {
    const uint8_t* b = reinterpret_cast<const sockaddr_in6*>(&address)->sin6_addr.s6_addr;
    static constexpr uint8_t kMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (memcmp(b, kMappedPrefix, sizeof kMappedPrefix) == 0) return IPAddress(b[12], b[13], b[14], b[15]);
  }
  return IPAddress();
}

/// @brief Whether the request came through the setup hotspot.
/// @param req The request.
bool viaHotspot(httpd_req_t* req) {
  IPAddress hotspot;
  {
    AppLock lock;
    if (!ctx.wifi->setupActive()) return false;
    hotspot = ctx.wifi->hotspotIp();
  }
  return localAddress(req) == hotspot;
}

/// @brief Whether a password given is the right one.
/// @param given The password given.
/// @param expected The right one; not empty.
bool sameSecret(const char* given, const char* expected) {
  // Takes as long for a near miss as for a far one.
  const size_t length = strlen(expected);
  unsigned diff = strlen(given) ^ length;
  for (size_t i = 0; given[i] != '\0'; ++i) diff |= static_cast<uint8_t>(given[i] ^ expected[i % length]);
  return diff == 0;
}

/// @brief The password from a basic auth header ("user:password"; the user
/// name doesn't matter).
/// @param header The `Authorization` header's value.
/// @param[out] decoded Room for the decoded header, which the password points
///   into.
/// @return The password, or nullptr if the header is malformed.
const char* passwordOf(const char* header, unsigned char (&decoded)[128]) {
  if (strncmp(header, "Basic ", 6) != 0) return nullptr;
  size_t length = 0;
  const char* encoded = header + 6;
  if (mbedtls_base64_decode(decoded, sizeof decoded - 1, &length,
                            reinterpret_cast<const unsigned char*>(encoded), strlen(encoded)) != 0) {
    return nullptr;
  }
  decoded[length] = '\0';
  const char* colon = strchr(reinterpret_cast<const char*>(decoded), ':');
  return colon != nullptr ? colon + 1 : nullptr;
}

/// @brief Whether a request may change things: it carries the web password,
/// none is set, or it came through the setup hotspot.
///
/// While the guard says wait after too many wrong passwords, nothing is
/// checked. A request without a password isn't counted as a guess.
/// @param req The request.
bool authorized(httpd_req_t* req) {
  if (viaHotspot(req)) return true;  // joining it took the password on the screen
  char password[sizeof Settings::webPassword];
  {
    AppLock lock;
    strlcpy(password, ctx.settings->webPassword, sizeof password);
  }
  if (password[0] == '\0') return true;

  // While the guard says wait, nothing is checked (refuse() says why).
  const uint32_t now = millis();
  if (guard.waitMs(now) > 0) return false;
  char header[160];
  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof header) != ESP_OK) {
    return false;  // no password given: not a guess
  }
  unsigned char decoded[128];
  const char* given = passwordOf(header, decoded);
  if (given != nullptr && sameSecret(given, password)) {
    guard.succeeded();
    return true;
  }
  guard.failed(now);
  loginFailures = guard.failures();
  Serial.printf("Web: wrong password (%u since start)\n", static_cast<unsigned>(guard.failures()));
  return false;
}

/// @brief Answers a request authorized() turned down.
///
/// While passwords are refused: 429 with `Retry-After`. Otherwise 401, with
/// the browser's password prompt only for requests not from the web page.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t refuse(httpd_req_t* req) {
  const uint32_t waitS = (guard.waitMs(millis()) + 999) / 1000;
  if (waitS > 0) {
    char retryAfter[12];
    snprintf(retryAfter, sizeof retryAfter, "%u", static_cast<unsigned>(waitS));
    httpd_resp_set_hdr(req, "Retry-After", retryAfter);
    char text[80];
    if (waitS < 60) {
      snprintf(text, sizeof text, "Too many wrong passwords. Try again in %u s.", static_cast<unsigned>(waitS));
    } else {
      snprintf(text, sizeof text, "Too many wrong passwords. Try again in %u min.",
               static_cast<unsigned>((waitS + 59) / 60));
    }
    return sendText(req, "429 Too Many Requests", text);
  }
  // The web page asks for the password itself (the browser's prompt would also
  // ask for a user name) and marks its requests; anything else, such as curl
  // or a browser opening the API directly, gets the usual Basic prompt.
  if (httpd_req_get_hdr_value_len(req, "X-Requested-With") == 0) {
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Air Monitor\", charset=\"UTF-8\"");
  }
  return sendText(req, "401 Unauthorized",
                  "This needs the web password shown on the monitor's Wi-Fi page.");
}

// ---------------------------------------------------------------- pages

/// @brief `GET /`: the dashboard, or the setup page when reached through the
/// setup hotspot.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getRoot(httpd_req_t* req) {
  return sendAsset(req, viaHotspot(req) ? web_assets::kSetupHtml : web_assets::kIndexHtml);
}

/// @brief `GET /setup`: the setup page.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getSetup(httpd_req_t* req) { return sendAsset(req, web_assets::kSetupHtml); }

/// @brief Any other route: 404, or through the setup hotspot a redirect to
/// the setup page.
///
/// That's the captive portal: whatever a phone probes for through the
/// hotspot is redirected to the setup page, which makes the phone open it.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t notFound(httpd_req_t* req, httpd_err_code_t) {
  if (!viaHotspot(req)) return sendText(req, "404 Not Found", "Not found");
  IPAddress ip;
  {
    AppLock lock;
    ip = ctx.wifi->hotspotIp();
  }
  char location[32];
  snprintf(location, sizeof location, "http://%s/", ip.toString().c_str());
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location);
  return httpd_resp_send(req, nullptr, 0);
}

// ---------------------------------------------------------------- state

/// @brief `GET /api/state`: readings, levels and status, as JSON.
///
/// The monitor's name, version, uptime and why it last started; each value
/// with its level and limits; the sensor's state; the battery (null without
/// one); Wi-Fi; the Home Assistant connection; and wrong web passwords.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getState(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  cJSON* r = root.get();
  {
    AppLock lock;
    const SensorSnapshot& s = *ctx.snapshot;
    const aq::AirQuality& air = *ctx.airQuality;
    const uint32_t now = millis();
    cJSON_AddStringToObject(r, "name", ctx.settings->name);
    cJSON_AddStringToObject(r, "host", device::hostname());
    cJSON_AddStringToObject(r, "version", FW_VERSION);
    cJSON_AddNumberToObject(r, "uptime", static_cast<double>(uptimeMs() / 1000));
    cJSON_AddStringToObject(r, "resetReason", resetReasonId(esp_reset_reason()));
    cJSON_AddStringToObject(r, "units", ctx.settings->fahrenheit ? "F" : "C");
    cJSON_AddStringToObject(r, "overall", levelId(air.overall()));

    cJSON* metrics = cJSON_AddObjectToObject(r, "metrics");
    for (size_t i = 0; i < aq::kMetricCount; ++i) {
      const Metric m = static_cast<Metric>(i);
      cJSON* item = cJSON_AddObjectToObject(metrics, aq::metricId(m));
      addNumber(item, "value", s.measurement[m], aq::kMetricDecimals[i]);
      cJSON_AddStringToObject(item, "level", levelId(air.level(m)));
      const aq::Thresholds& t = air.thresholds(m);
      cJSON_AddItemToObject(item, "warn", limits(t.warnLow, t.warnHigh));
      cJSON_AddItemToObject(item, "alarm", limits(t.alarmLow, t.alarmHigh));
    }

    cJSON* sensor = cJSON_AddObjectToObject(r, "sensor");
    cJSON_AddStringToObject(sensor, "state", sensorStateId(s.state));
    cJSON_AddStringToObject(sensor, "activity", s.activity);
    cJSON_AddStringToObject(sensor, "product", s.product);
    cJSON_AddStringToObject(sensor, "serial", s.serial);
    char firmware[8];
    snprintf(firmware, sizeof firmware, "%u.%u", s.firmwareMajor, s.firmwareMinor);
    cJSON_AddStringToObject(sensor, "firmware", firmware);
    if (s.state == SensorState::Measuring) {
      cJSON_AddNumberToObject(sensor, "measuringFor",
                              static_cast<double>((uptimeMs() - s.measuringSinceMs) / 1000));
    } else {
      cJSON_AddNullToObject(sensor, "measuringFor");
    }
    cJSON_AddNumberToObject(sensor, "status", s.deviceStatus);
    cJSON_AddNumberToObject(sensor, "error", s.lastError);
    if (s.confirmedAltitudeM >= 0) {
      cJSON_AddNumberToObject(sensor, "altitude", s.confirmedAltitudeM);
    } else {
      cJSON_AddNullToObject(sensor, "altitude");
    }

    if (!ctx.battery->present() || std::isnan(s.batteryVolts)) {
      cJSON_AddNullToObject(r, "battery");
    } else {
      cJSON* battery = cJSON_AddObjectToObject(r, "battery");
      addNumber(battery, "volts", s.batteryVolts, 2);
      addNumber(battery, "percent", ctx.battery->percent(), 0);
      cJSON_AddStringToObject(battery, "state", batteryStateId(ctx.battery->state()));
      cJSON_AddNumberToObject(battery, "warning", ctx.battery->warning());
    }

    cJSON* wifi = cJSON_AddObjectToObject(r, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", ctx.wifi->ssid());
    cJSON_AddNumberToObject(wifi, "rssi", ctx.wifi->rssi());
    addMqttState(cJSON_AddObjectToObject(r, "mqtt"), *ctx.mqtt);

    cJSON* login = cJSON_AddObjectToObject(r, "login");
    cJSON_AddNumberToObject(login, "failed", guard.failures());
    cJSON_AddNumberToObject(login, "wait", (guard.waitMs(now) + 999) / 1000);
  }
  return sendJson(req, r);
}

/// @brief `GET /api/history?metric=<id>&range=hour|day|week`: one value's
/// history, as JSON.
///
/// The hour comes in 10 s means, the day in 1 min means and the week in
/// 5 min means: `step` is the time between them in s, `capacity` how many
/// the range holds, and `values` those there are so far, oldest first, null
/// where there was no data. Answers 400 for an unknown metric or range.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getHistory(httpd_req_t* req) {
  char query[64] = "";
  char id[8] = "";
  char range[8] = "hour";
  httpd_req_get_url_query_str(req, query, sizeof query);
  httpd_query_key_value(query, "metric", id, sizeof id);
  httpd_query_key_value(query, "range", range, sizeof range);
  Metric metric;
  if (!metricFromId(id, metric)) return badRequest(req, "Unknown metric");
  // The hour in 10 s means, the day in 1 min means, and the week in 5 min
  // means: plenty for a chart, and a fifth of the data.
  using aq::MetricHistory;
  static constexpr struct {
    const char* id;
    bool coarse;
    size_t span;  // in samples of the ring
    size_t group;
  } kRanges[] = {
      {"hour", false, MetricHistory::Fine::capacity(), 1},
      {"day", true, 24 * 60, 1},
      {"week", true, MetricHistory::Coarse::capacity(), 5},
  };
  const auto* r = std::find_if(std::begin(kRanges), std::end(kRanges),
                               [&](const auto& candidate) { return strcmp(candidate.id, range) == 0; });
  if (r == std::end(kRanges)) return badRequest(req, "Unknown range");
  const size_t capacity = (r->span + r->group - 1) / r->group;
  const uint32_t step =
      (r->coarse ? MetricHistory::kCoarsePeriodMs : MetricHistory::kFinePeriodMs) / 1000 * r->group;

  // Copy under the lock, then send without holding it (the client may be slow).
  std::unique_ptr<float[]> values(new (std::nothrow) float[capacity]);
  if (!values) return httpd_resp_send_500(req);
  size_t count = 0;
  {
    AppLock lock;
    if (r->coarse) {
      count = aq::groupMeans(ctx.history->coarse(metric), r->span, r->group, values.get());
    } else {
      count = aq::groupMeans(ctx.history->fine(metric), r->span, r->group, values.get());
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const int decimals = aq::kMetricDecimals[aq::idx(metric)];
  char buffer[512];
  int n = snprintf(buffer, sizeof buffer, "{\"metric\":\"%s\",\"step\":%u,\"capacity\":%u,\"values\":[",
                   id, static_cast<unsigned>(step), static_cast<unsigned>(capacity));
  for (size_t i = 0; i < count; ++i) {
    if (n > static_cast<int>(sizeof buffer) - 24) {
      if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) return ESP_FAIL;
      n = 0;
    }
    const char* separator = i == 0 ? "" : ",";
    if (std::isnan(values[i])) {
      n += snprintf(buffer + n, sizeof buffer - n, "%snull", separator);
    } else {
      n += snprintf(buffer + n, sizeof buffer - n, "%s%.*f", separator, decimals, values[i]);
    }
  }
  n += snprintf(buffer + n, sizeof buffer - n, "]}");
  if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) return ESP_FAIL;
  return httpd_resp_send_chunk(req, nullptr, 0);
}

/// @brief `GET /api/history.csv`: every value's 1 min means, oldest first, as
/// CSV.
///
/// The monitor has no clock, so the first column says how many seconds ago
/// each minute started; the web page turns that into times.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getHistoryCsv(httpd_req_t* req) {
  using Coarse = aq::MetricHistory::Coarse;
  static constexpr const char* kColumns[aq::kMetricCount] = {
      "PM1 (ug/m3)", "PM2.5 (ug/m3)", "PM4 (ug/m3)", "PM10 (ug/m3)",    "CO2 (ppm)",
      "HCHO (ppb)",  "VOC index",     "NOx index",   "Temperature (C)", "Humidity (%)"};
  constexpr size_t kRows = Coarse::capacity();
  constexpr uint32_t kPeriodS = aq::MetricHistory::kCoarsePeriodMs / 1000;
  constexpr size_t kTextBytes = 4096;
  constexpr size_t kTemperature = aq::idx(Metric::Temperature);

  // Copy under the lock (~400 KB, so into PSRAM), then send without holding it.
  std::unique_ptr<float, decltype(&free)> copy(
      static_cast<float*>(ps_malloc(kRows * aq::kMetricCount * sizeof(float))), &free);
  std::unique_ptr<char[]> text(new (std::nothrow) char[kTextBytes]);
  if (!copy || !text) return httpd_resp_send_500(req);
  float* values = copy.get();  // one metric after the other
  size_t count = 0;
  uint32_t sinceNewestS = 0;  // since the newest mean's minute ended
  bool fahrenheit = false;
  {
    AppLock lock;
    for (size_t m = 0; m < aq::kMetricCount; ++m) {
      const Coarse& ring = ctx.history->coarse(static_cast<Metric>(m));
      count = ring.size();
      for (size_t i = 0; i < count; ++i) values[m * kRows + i] = ring[i];
    }
    sinceNewestS = (millis() - ctx.history->coarseStartMs()) / 1000;
    fahrenheit = ctx.settings->fahrenheit;
  }

  httpd_resp_set_type(req, "text/csv");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"air-monitor-history.csv\"");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char* buffer = text.get();
  int n = snprintf(buffer, kTextBytes, "Seconds ago");
  for (size_t m = 0; m < aq::kMetricCount; ++m) {
    const char* name = m == kTemperature && fahrenheit ? "Temperature (F)" : kColumns[m];
    n += snprintf(buffer + n, kTextBytes - n, ",%s", name);
  }
  n += snprintf(buffer + n, kTextBytes - n, "\r\n");
  for (size_t i = 0; i < count; ++i) {
    if (n > static_cast<int>(kTextBytes) - 160) {  // room for a row
      if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) return ESP_FAIL;
      n = 0;
    }
    n += snprintf(buffer + n, kTextBytes - n, "%u",
                  static_cast<unsigned>(sinceNewestS + (count - i) * kPeriodS));
    for (size_t m = 0; m < aq::kMetricCount; ++m) {
      float v = values[m * kRows + i];
      if (std::isnan(v)) {
        buffer[n++] = ',';  // no data that minute
        continue;
      }
      if (m == kTemperature && fahrenheit) v = v * 9 / 5 + 32;
      n += snprintf(buffer + n, kTextBytes - n, ",%.*f", aq::kMetricDecimals[m], v);
    }
    n += snprintf(buffer + n, kTextBytes - n, "\r\n");
  }
  if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) return ESP_FAIL;
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---------------------------------------------------------------- settings

/// @brief Adds the settings to a JSON object; the web password only as
/// whether one is set.
/// @param r The object.
/// @param s The settings.
void addSettings(cJSON* r, const Settings& s) {
  cJSON_AddStringToObject(r, "name", s.name);
  cJSON_AddNumberToObject(r, "brightness", s.brightness);
  cJSON_AddNumberToObject(r, "dimAfter", s.dimAfterS);
  cJSON_AddStringToObject(r, "idleScreen", s.screenOffWhenIdle ? "off" : "dim");
  cJSON_AddBoolToObject(r, "fahrenheit", s.fahrenheit);
  cJSON_AddStringToObject(r, "led", ledId(s.led));
  cJSON_AddStringToObject(r, "silence", silenceId(s.silence));
  addNumber(r, "tempOffset", s.tempOffsetC, 1);
  cJSON_AddNumberToObject(r, "altitude", s.altitudeM);
  cJSON_AddBoolToObject(r, "co2SelfCalibration", s.co2SelfCalibration);
  cJSON_AddBoolToObject(r, "wifiEnabled", s.wifiEnabled);
  cJSON_AddBoolToObject(r, "passwordSet", s.webPassword[0] != '\0');
}

/// @brief `GET /api/settings`: the settings, as JSON.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getSettings(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  {
    AppLock lock;
    addSettings(root.get(), *ctx.settings);
  }
  return sendJson(req, root.get());
}

/// @brief Whether text is all printable ASCII.
/// @param text The text.
bool printable(const char* text) {
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < 0x20 || *c > 0x7e) return false;
  }
  return true;
}

/// @brief Applies the settings present in `json` to `s`.
/// @param json The request.
/// @param[in,out] s The settings to change.
/// @return nullptr, or what's wrong, for people.
const char* parseSettings(const cJSON* json, Settings& s) {
  const cJSON* f;
  if (const char* name = stringField(json, "name")) {
    const size_t length = strlen(name);
    if (length == 0 || length >= sizeof s.name) return "Name: 1 to 31 characters";
    strlcpy(s.name, name, sizeof s.name);
  }
  if ((f = field(json, "brightness"))) {
    if (!cJSON_IsNumber(f) || f->valuedouble < 10 || f->valuedouble > 100) {
      return "Brightness: 10 to 100 %";
    }
    s.brightness = static_cast<uint8_t>(std::lround(f->valuedouble));
  }
  if ((f = field(json, "dimAfter"))) {
    const auto& options = kDimOptionsS;
    if (!cJSON_IsNumber(f) || std::find(std::begin(options), std::end(options), f->valueint) == std::end(options)) {
      return "Unsupported dimming delay";
    }
    s.dimAfterS = static_cast<uint16_t>(f->valueint);
  }
  if ((f = field(json, "idleScreen"))) {
    const char* idle = cJSON_IsString(f) ? f->valuestring : "";
    if (strcmp(idle, "dim") != 0 && strcmp(idle, "off") != 0) return "Idle screen: dim or off";
    s.screenOffWhenIdle = strcmp(idle, "off") == 0;
  }
  if ((f = field(json, "fahrenheit"))) {
    if (!cJSON_IsBool(f)) return "Units: true or false";
    s.fahrenheit = cJSON_IsTrue(f);
  }
  if ((f = field(json, "led"))) {
    const char* led = cJSON_IsString(f) ? f->valuestring : "";
    if (strcmp(led, "off") == 0) {
      s.led = LedMode::Off;
    } else if (strcmp(led, "dim") == 0) {
      s.led = LedMode::Dim;
    } else if (strcmp(led, "bright") == 0) {
      s.led = LedMode::Bright;
    } else {
      return "Status LED: off, dim or bright";
    }
  }
  if ((f = field(json, "silence"))) {
    const char* silence = cJSON_IsString(f) ? f->valuestring : "";
    if (strcmp(silence, "off") == 0) {
      s.silence = Silence::Off;
    } else if (strcmp(silence, "warnings") == 0) {
      s.silence = Silence::Warnings;
    } else if (strcmp(silence, "all") == 0) {
      s.silence = Silence::All;
    } else {
      return "Silence: off, warnings or all";
    }
  }
  if ((f = field(json, "tempOffset"))) {
    if (!cJSON_IsNumber(f) || f->valuedouble < -10 || f->valuedouble > 10) {
      return "Temperature offset: -10 to 10 \xC2\xB0" "C";
    }
    s.tempOffsetC = static_cast<float>(rounded(static_cast<float>(f->valuedouble), 1));
  }
  if ((f = field(json, "altitude"))) {
    if (!cJSON_IsNumber(f) || f->valuedouble < 0 || f->valuedouble > 3000) {
      return "Altitude: 0 to 3000 m";
    }
    s.altitudeM = static_cast<uint16_t>(std::lround(f->valuedouble));
  }
  if ((f = field(json, "co2SelfCalibration"))) {
    if (!cJSON_IsBool(f)) return "CO2 self-calibration: true or false";
    s.co2SelfCalibration = cJSON_IsTrue(f);
  }
  if ((f = field(json, "wifiEnabled"))) {
    if (!cJSON_IsBool(f)) return "Wi-Fi: true or false";
    s.wifiEnabled = cJSON_IsTrue(f);
  }
  if ((f = field(json, "webPassword"))) {
    const char* password = cJSON_IsString(f) ? f->valuestring : nullptr;
    if (password == nullptr || strlen(password) >= sizeof s.webPassword || !printable(password)) {
      return "Web password: up to 32 plain ASCII characters";
    }
    strlcpy(s.webPassword, password, sizeof s.webPassword);
  }
  return nullptr;
}

/// @brief `POST /api/settings` (web password): changes the settings present
/// in the JSON body.
///
/// Answers with all the settings, or 400 and what's wrong, changing none.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t postSettings(httpd_req_t* req) {
  if (!fromPage(req)) return notFromPage(req);
  if (!authorized(req)) return refuse(req);
  const Json json = readJson(req);
  if (!json) return badRequest(req, "Expected a JSON object");
  // Replies go out after the lock is released: a slow client mustn't stall the loop.
  Json reply(cJSON_CreateObject());
  const char* error;
  {
    AppLock lock;
    Settings changed = *ctx.settings;
    error = parseSettings(json.get(), changed);
    if (error == nullptr) {
      *ctx.settings = changed;
      ctx.ui->settingsChanged();
      addSettings(reply.get(), changed);
    }
  }
  return error ? badRequest(req, error) : sendJson(req, reply.get());
}

/// @brief `POST /api/action` (web password): one of the sensor's commands, or
/// a restart.
///
/// The body is `{"action": ...}` with `cleanFan`, `heatHumiditySensor`,
/// `calibrateCo2` (with `"ppm"`, 350 to 2000), `resetCo2Calibration`,
/// `restartVocLearning`, `clearErrors` or `restart`. Answers 204, or 400 and
/// what's wrong.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t postAction(httpd_req_t* req) {
  if (!fromPage(req)) return notFromPage(req);
  if (!authorized(req)) return refuse(req);
  const Json json = readJson(req);
  const char* action = json ? stringField(json.get(), "action") : nullptr;
  if (action == nullptr) return badRequest(req, "Expected {\"action\": ...}");
  if (strcmp(action, "cleanFan") == 0) {
    ctx.sensor->cleanFan();
  } else if (strcmp(action, "heatHumiditySensor") == 0) {
    ctx.sensor->heatHumiditySensor();
  } else if (strcmp(action, "resetCo2Calibration") == 0) {
    ctx.sensor->resetCo2Calibration();
  } else if (strcmp(action, "restartVocLearning") == 0) {
    ctx.sensor->restartVocLearning();
  } else if (strcmp(action, "clearErrors") == 0) {
    ctx.sensor->clearErrors();
  } else if (strcmp(action, "calibrateCo2") == 0) {
    const cJSON* ppm = field(json.get(), "ppm");
    if (!cJSON_IsNumber(ppm) || ppm->valuedouble < 350 || ppm->valuedouble > 2000) {
      return badRequest(req, "Reference CO2: 350 to 2000 ppm");
    }
    ctx.sensor->recalibrateCo2(static_cast<uint16_t>(std::lround(ppm->valuedouble)));
  } else if (strcmp(action, "restart") == 0) {
    sys::restartLater();
  } else {
    return badRequest(req, "Unknown action");
  }
  return sendOk(req);
}

// ---------------------------------------------------------------- Wi-Fi

/// @brief Adds a saved network's fixed address to its JSON, or null for DHCP.
/// @param item The network's JSON.
/// @param a The address.
void addAddress(cJSON* item, const FixedAddress& a) {
  if (!a.set()) {
    cJSON_AddNullToObject(item, "address");
    return;
  }
  cJSON* address = cJSON_AddObjectToObject(item, "address");
  char text[16];
  const struct {
    const char* key;
    uint32_t value;
  } parts[] = {{"ip", a.ip}, {"subnet", a.subnet}, {"gateway", a.gateway}, {"dns", a.dns}};
  for (const auto& part : parts) {
    text[0] = '\0';
    if (part.value != 0) net::formatIpv4(part.value, text, sizeof text);
    cJSON_AddStringToObject(address, part.key, text);
  }
}

/// @brief `GET /api/wifi`: Wi-Fi, as JSON.
///
/// Its state, the saved networks, the last scan and the setup's progress;
/// through the setup hotspot also the web password.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getWifi(httpd_req_t* req) {
  const bool hotspot = viaHotspot(req);
  Json root(cJSON_CreateObject());
  cJSON* r = root.get();
  {
    AppLock lock;
    const WifiService& w = *ctx.wifi;
    cJSON_AddBoolToObject(r, "enabled", ctx.settings->wifiEnabled);
    cJSON_AddStringToObject(r, "state", wifiStateId(w.state()));
    cJSON_AddStringToObject(r, "ssid", w.ssid());
    cJSON_AddNumberToObject(r, "rssi", w.rssi());
    cJSON_AddStringToObject(r, "ip", w.state() == WifiState::Connected ? w.ip().toString().c_str() : "");
    const IPAddress routerIp = w.routerIp();
    cJSON_AddStringToObject(r, "routerIp", uint32_t(routerIp) != 0 ? routerIp.toString().c_str() : "");
    const String host = String(device::hostname()) + ".local";
    cJSON_AddStringToObject(r, "host", host.c_str());

    cJSON* saved = cJSON_AddArrayToObject(r, "saved");
    for (size_t i = 0; i < w.saved().size(); ++i) {
      cJSON* item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "ssid", w.saved()[i].ssid);
      cJSON_AddBoolToObject(item, "hidden", w.saved()[i].hidden);
      addAddress(item, w.saved()[i].address);
      cJSON_AddItemToArray(saved, item);
    }
    cJSON_AddBoolToObject(r, "scanning", w.scanning());
    cJSON* networks = cJSON_AddArrayToObject(r, "networks");
    for (size_t i = 0; i < w.scannedCount(); ++i) {
      const ScannedNetwork& n = w.scanned(i);
      cJSON* item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "ssid", n.ssid);
      cJSON_AddNumberToObject(item, "rssi", n.rssi);
      cJSON_AddBoolToObject(item, "secure", n.secure);
      cJSON_AddItemToArray(networks, item);
    }
    cJSON* setup = cJSON_AddObjectToObject(r, "setup");
    cJSON_AddStringToObject(setup, "state", setupStateId(w.setupState()));
    cJSON_AddStringToObject(setup, "ssid", w.trialSsid());
    cJSON_AddStringToObject(setup, "message", w.setupMessage());
    // Whoever joined the hotspot has seen the device's screen anyway.
    if (hotspot) cJSON_AddStringToObject(r, "webPassword", ctx.settings->webPassword);
  }
  return sendJson(req, r);
}

/// @brief Reads a network's name, password and hidden flag from a request.
/// @param json The request.
/// @param[out] network The network.
/// @return nullptr, or what's wrong, for people.
const char* parseNetwork(const cJSON* json, WifiCredentials& network) {
  const char* ssid = stringField(json, "ssid");
  const char* password = stringField(json, "password");
  if (password == nullptr) password = "";
  if (ssid == nullptr || ssid[0] == '\0' || strlen(ssid) >= sizeof network.ssid) {
    return "Network name: 1 to 32 characters";
  }
  const size_t length = strlen(password);
  if (length != 0 && (length < 8 || length >= sizeof network.password)) {
    return "Password: 8 to 64 characters, or none for an open network";
  }
  strlcpy(network.ssid, ssid, sizeof network.ssid);
  strlcpy(network.password, password, sizeof network.password);
  network.hidden = cJSON_IsTrue(field(json, "hidden"));
  return nullptr;
}

/// @brief Reads a fixed address from a request; an empty or missing "ip"
/// means DHCP.
/// @param json The request.
/// @param[out] out The address; FixedAddress() for DHCP, or if something's
///   wrong.
/// @return nullptr, or what's wrong, for people.
const char* parseAddress(const cJSON* json, FixedAddress& out) {
  out = FixedAddress();
  const char* ip = stringField(json, "ip");
  if (ip == nullptr || ip[0] == '\0') return nullptr;
  const char* subnet = stringField(json, "subnet");
  const char* gateway = stringField(json, "gateway");
  const char* dns = stringField(json, "dns");
  FixedAddress a;
  if (!net::parseIpv4(ip, a.ip)) return "IP address: four numbers such as 192.168.1.20";
  if (subnet == nullptr || !net::parseIpv4(subnet, a.subnet)) {
    return "Subnet mask: four numbers such as 255.255.255.0";
  }
  if (gateway == nullptr || !net::parseIpv4(gateway, a.gateway)) {
    return "Gateway: four numbers, usually your router's address";
  }
  if (dns != nullptr && dns[0] != '\0' && !net::parseIpv4(dns, a.dns)) {
    return "DNS server: four numbers, or empty for the gateway";
  }
  if (const char* error = net::checkFixedAddress(a.ip, a.subnet, a.gateway, a.dns)) return error;
  out = a;
  return nullptr;
}

/// @brief Runs a `POST /api/wifi` action; call under AppLock.
/// @param action The action.
/// @param json The request, with what the action needs.
/// @return nullptr, or what's wrong, for people.
const char* wifiAction(const char* action, const cJSON* json) {
  WifiService& w = *ctx.wifi;
  if (strcmp(action, "scan") == 0) {
    w.requestScan();
  } else if (strcmp(action, "connect") == 0 || strcmp(action, "save") == 0) {
    WifiCredentials network;
    if (const char* error = parseNetwork(json, network)) return error;
    if (action[0] == 'c') {
      // Trying a network drops the current connection, so only during setup.
      if (!w.setupActive()) return "Start Wi-Fi setup on the monitor first";
      w.tryNetwork(network);
    } else {
      w.save(network);
    }
  } else if (strcmp(action, "forget") == 0) {
    const char* ssid = stringField(json, "ssid");
    if (ssid == nullptr || !w.forget(ssid)) return "No such network saved";
  } else if (strcmp(action, "address") == 0) {
    const char* ssid = stringField(json, "ssid");
    FixedAddress address;
    if (const char* error = parseAddress(json, address)) return error;
    if (ssid == nullptr || !w.setAddress(ssid, address)) return "No such network saved";
  } else if (strcmp(action, "finish") == 0) {
    w.stopSetupSoon();
  } else {
    return "Unknown action";
  }
  return nullptr;
}

/// @brief `POST /api/wifi` (web password): changes the saved networks, or
/// runs the setup.
///
/// The body is `{"action": ...}` with `scan`; `connect` (only during setup)
/// or `save`, with `"ssid"`, `"password"` and `"hidden"`; `forget`, with
/// `"ssid"`; `address`, with `"ssid"`, `"ip"`, `"subnet"`, `"gateway"` and
/// `"dns"`; or `finish`, which closes the setup hotspot. Answers 204, or 400
/// and what's wrong.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t postWifi(httpd_req_t* req) {
  if (!fromPage(req)) return notFromPage(req);
  if (!authorized(req)) return refuse(req);
  const Json json = readJson(req);
  const char* action = json ? stringField(json.get(), "action") : nullptr;
  if (action == nullptr) return badRequest(req, "Expected {\"action\": ...}");
  const char* error;
  {
    AppLock lock;
    error = wifiAction(action, json.get());
  }
  return error ? badRequest(req, error) : sendOk(req);
}

// ---------------------------------------------------------------- Home Assistant

/// @brief Adds the Home Assistant settings and the connection's state to a
/// JSON object; the password only as whether one is set.
/// @param r The object.
/// @param s The settings.
/// @param mqtt The MQTT client.
void addMqtt(cJSON* r, const Settings& s, const MqttService& mqtt) {
  cJSON_AddBoolToObject(r, "enabled", s.mqttEnabled);
  cJSON_AddStringToObject(r, "host", s.mqttHost);
  cJSON_AddNumberToObject(r, "port", s.mqttPort);
  cJSON_AddStringToObject(r, "user", s.mqttUser);
  cJSON_AddBoolToObject(r, "passwordSet", s.mqttPassword[0] != '\0');
  addMqttState(r, mqtt);
}

/// @brief `GET /api/mqtt`: the Home Assistant broker settings and the
/// connection's state, as JSON.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getMqtt(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  {
    AppLock lock;
    addMqtt(root.get(), *ctx.settings, *ctx.mqtt);
  }
  return sendJson(req, root.get());
}

/// @brief Applies the Home Assistant settings present in `json` to `s`.
/// @param json The request.
/// @param[in,out] s The settings to change.
/// @return nullptr, or what's wrong, for people.
const char* parseMqtt(const cJSON* json, Settings& s) {
  const cJSON* f;
  if ((f = field(json, "enabled"))) {
    if (!cJSON_IsBool(f)) return "Enabled: true or false";
    s.mqttEnabled = cJSON_IsTrue(f);
  }
  if ((f = field(json, "host"))) {
    const char* host = cJSON_IsString(f) ? f->valuestring : nullptr;
    if (host == nullptr || strlen(host) >= sizeof s.mqttHost || !printable(host) ||
        strchr(host, ' ') != nullptr) {
      return "Broker address: a name or IP address of up to 64 characters";
    }
    strlcpy(s.mqttHost, host, sizeof s.mqttHost);
  }
  if ((f = field(json, "port"))) {
    if (!cJSON_IsNumber(f) || f->valuedouble < 1 || f->valuedouble > 65535) {
      return "Port: 1 to 65535";
    }
    s.mqttPort = static_cast<uint16_t>(std::lround(f->valuedouble));
  }
  if ((f = field(json, "user"))) {
    const char* user = cJSON_IsString(f) ? f->valuestring : nullptr;
    if (user == nullptr || strlen(user) >= sizeof s.mqttUser || !printable(user)) {
      return "User name: up to 64 plain ASCII characters";
    }
    strlcpy(s.mqttUser, user, sizeof s.mqttUser);
  }
  if ((f = field(json, "password"))) {
    const char* password = cJSON_IsString(f) ? f->valuestring : nullptr;
    if (password == nullptr || strlen(password) >= sizeof s.mqttPassword || !printable(password)) {
      return "Password: up to 64 plain ASCII characters";
    }
    strlcpy(s.mqttPassword, password, sizeof s.mqttPassword);
  }
  if (s.mqttEnabled && s.mqttHost[0] == '\0') return "Enter the broker's address";
  return nullptr;
}

/// @brief `POST /api/mqtt` (web password): changes the Home Assistant
/// settings present in the JSON body.
///
/// Answers like `GET /api/mqtt`, or 400 and what's wrong, changing none.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t postMqtt(httpd_req_t* req) {
  if (!fromPage(req)) return notFromPage(req);
  if (!authorized(req)) return refuse(req);
  const Json json = readJson(req);
  if (!json) return badRequest(req, "Expected a JSON object");
  Json reply(cJSON_CreateObject());
  const char* error;
  {
    AppLock lock;
    Settings changed = *ctx.settings;
    error = parseMqtt(json.get(), changed);
    if (error == nullptr) {
      *ctx.settings = changed;  // the MQTT client restarts with them
      ctx.ui->settingsChanged();  // saves them
      addMqtt(reply.get(), changed, *ctx.mqtt);
    }
  }
  return error ? badRequest(req, error) : sendJson(req, reply.get());
}

// ---------------------------------------------------------------- firmware

/// @brief `GET /api/auth`: 204 if the request carries the web password (or
/// needs none); otherwise as refuse() answers.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getAuth(httpd_req_t* req) {
  return authorized(req) ? sendOk(req) : refuse(req);
}

/// @brief `GET /api/ota`: an update's state and, while it waits for the
/// monitor, the seconds left to answer there, as JSON.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection.
esp_err_t getOta(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  cJSON_AddStringToObject(root.get(), "state", otaStateId(ctx.ota->state()));
  cJSON_AddNumberToObject(root.get(), "secondsLeft", ctx.ota->secondsLeft(millis()));
  return sendJson(req, root.get());
}

/// @brief `POST /api/ota` (web password): a firmware image as the body,
/// written to flash as it arrives (see Ota).
///
/// Answers 202 once it's stored, saying how to confirm it on the monitor, or
/// 400 and why it failed.
/// @param req The request.
/// @return ESP_OK, or an error to close the connection, e.g. when it was lost.
esp_err_t postOta(httpd_req_t* req) {
  if (!fromPage(req)) return notFromPage(req);
  if (!authorized(req)) return refuse(req);
  if (const char* error = ctx.ota->start(req->content_len)) return badRequest(req, error);
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kOtaChunkBytes]);
  if (!buffer) {
    ctx.ota->abort();
    return httpd_resp_send_500(req);
  }
  size_t remaining = req->content_len;
  while (remaining > 0) {
    // Fill the whole buffer, so the first write holds the complete image header.
    const size_t wanted = std::min(remaining, kOtaChunkBytes);
    size_t received = 0;
    int retries = 0;
    while (received < wanted) {
      const int n = httpd_req_recv(req, reinterpret_cast<char*>(buffer.get()) + received,
                                   wanted - received);
      if (n == HTTPD_SOCK_ERR_TIMEOUT && ++retries <= kRecvRetries) continue;
      if (n <= 0) {
        ctx.ota->abort();
        return ESP_FAIL;  // connection lost: close it
      }
      received += n;
    }
    if (const char* error = ctx.ota->write(buffer.get(), received)) return badRequest(req, error);
    remaining -= received;
  }
  if (const char* error = ctx.ota->finish()) return badRequest(req, error);
  char text[112];
  snprintf(text, sizeof text,
           "Stored. To install it, hold any button on the monitor for %u s within %u minutes.",
           static_cast<unsigned>(Ota::kConfirmHoldMs / 1000),
           static_cast<unsigned>(Ota::kConfirmWindowMs / 60000));
  return sendText(req, "202 Accepted", text);
}

}  // namespace

bool begin(const Context& context) {
  if (server != nullptr) return true;
  ctx = context;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;
  config.lru_purge_enable = true;  // phones leave idle connections open
  config.stack_size = 8192;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.println("Web: server did not start");
    server = nullptr;
    return false;
  }
  static const httpd_uri_t routes[] = {
      {"/", HTTP_GET, getRoot, nullptr},
      {"/setup", HTTP_GET, getSetup, nullptr},
      {"/api/state", HTTP_GET, getState, nullptr},
      {"/api/history", HTTP_GET, getHistory, nullptr},
      {"/api/history.csv", HTTP_GET, getHistoryCsv, nullptr},
      {"/api/settings", HTTP_GET, getSettings, nullptr},
      {"/api/settings", HTTP_POST, postSettings, nullptr},
      {"/api/action", HTTP_POST, postAction, nullptr},
      {"/api/wifi", HTTP_GET, getWifi, nullptr},
      {"/api/wifi", HTTP_POST, postWifi, nullptr},
      {"/api/mqtt", HTTP_GET, getMqtt, nullptr},
      {"/api/mqtt", HTTP_POST, postMqtt, nullptr},
      {"/api/auth", HTTP_GET, getAuth, nullptr},
      {"/api/ota", HTTP_GET, getOta, nullptr},
      {"/api/ota", HTTP_POST, postOta, nullptr},
  };
  for (const httpd_uri_t& route : routes) httpd_register_uri_handler(server, &route);
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notFound);
  Serial.printf("Web: http://%s.local/\n", device::hostname());
  return true;
}

bool started() { return server != nullptr; }

uint32_t failedLogins() { return loginFailures; }

}  // namespace web
