// SPDX-License-Identifier: GPL-3.0-or-later

#include "Web.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_http_server.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

#include "AppLock.h"
#include "Device.h"
#include "System.h"
#include "net/Json.h"
#include "ui/Ui.h"
#include "web/WebAssets.h"

using aq::Level;
using aq::Metric;

namespace web {

namespace {

constexpr size_t kMaxBodyBytes = 2048;
constexpr size_t kOtaChunkBytes = 4096;
constexpr int kRecvRetries = 5;

Context ctx;
httpd_handle_t server = nullptr;

// ---------------------------------------------------------------- helpers

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

void addMqttState(cJSON* object, const MqttService& mqtt) {
  cJSON_AddStringToObject(object, "state", mqttStateId(mqtt.state()));
  cJSON_AddStringToObject(object, "error", mqtt.error());
}

const char* ledId(LedMode mode) {
  return mode == LedMode::Off ? "off" : mode == LedMode::Dim ? "dim" : "bright";
}

bool metricFromId(const char* id, Metric& out) {
  for (size_t i = 0; i < aq::kMetricCount; ++i) {
    if (strcmp(id, aq::kMetricIds[i]) == 0) {
      out = static_cast<Metric>(i);
      return true;
    }
  }
  return false;
}

// [low, high] limits, null where a side has none.
cJSON* limits(float low, float high) {
  cJSON* array = cJSON_CreateArray();
  for (float v : {low, high}) {
    cJSON_AddItemToArray(array, std::isnan(v) ? cJSON_CreateNull() : cJSON_CreateNumber(rounded(v, 1)));
  }
  return array;
}

const cJSON* field(const cJSON* object, const char* key) {
  return cJSON_GetObjectItemCaseSensitive(object, key);
}

const char* stringField(const cJSON* object, const char* key) {
  const cJSON* f = field(object, key);
  return cJSON_IsString(f) ? f->valuestring : nullptr;
}

esp_err_t sendJson(httpd_req_t* req, const cJSON* json) {
  const JsonText text(cJSON_PrintUnformatted(json));
  if (!text) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, text.get());
}

esp_err_t sendText(httpd_req_t* req, const char* status, const char* text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(req, text);
}

esp_err_t badRequest(httpd_req_t* req, const char* reason) {
  return sendText(req, "400 Bad Request", reason);
}

esp_err_t sendOk(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, nullptr, 0);
}

esp_err_t sendAsset(httpd_req_t* req, const WebAsset& asset) {
  httpd_resp_set_type(req, asset.type);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, reinterpret_cast<const char*>(asset.data), asset.size);
}

// Reads a JSON request body.
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

// Local address the request came in on. The server listens on IPv6, where
// IPv4 clients show up as IPv4-mapped addresses.
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

// Whether the request came through the setup hotspot.
bool viaHotspot(httpd_req_t* req) {
  IPAddress hotspot;
  {
    AppLock lock;
    if (!ctx.wifi->setupActive()) return false;
    hotspot = ctx.wifi->hotspotIp();
  }
  return localAddress(req) == hotspot;
}

bool sameSecret(const char* given, const char* expected) {
  // Takes as long for a near miss as for a far one.
  const size_t length = strlen(expected);
  unsigned diff = strlen(given) ^ length;
  for (size_t i = 0; given[i] != '\0'; ++i) diff |= static_cast<uint8_t>(given[i] ^ expected[i % length]);
  return diff == 0;
}

bool authorized(httpd_req_t* req) {
  if (viaHotspot(req)) return true;  // joining it took the password on the screen
  char password[sizeof Settings::webPassword];
  {
    AppLock lock;
    strlcpy(password, ctx.settings->webPassword, sizeof password);
  }
  if (password[0] == '\0') return true;

  char header[160];
  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof header) != ESP_OK) return false;
  if (strncmp(header, "Basic ", 6) != 0) return false;
  unsigned char decoded[128];
  size_t length = 0;
  const char* encoded = header + 6;
  if (mbedtls_base64_decode(decoded, sizeof decoded - 1, &length,
                            reinterpret_cast<const unsigned char*>(encoded), strlen(encoded)) != 0) {
    return false;
  }
  decoded[length] = '\0';
  // "user:password"; the user name doesn't matter.
  const char* colon = strchr(reinterpret_cast<const char*>(decoded), ':');
  return colon != nullptr && sameSecret(colon + 1, password);
}

esp_err_t askForPassword(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Air Monitor\", charset=\"UTF-8\"");
  return sendText(req, "401 Unauthorized",
                  "This needs the web password shown on the monitor's Wi-Fi page.");
}

// ---------------------------------------------------------------- pages

esp_err_t getRoot(httpd_req_t* req) {
  return sendAsset(req, viaHotspot(req) ? web_assets::kSetupHtml : web_assets::kIndexHtml);
}

esp_err_t getSetup(httpd_req_t* req) { return sendAsset(req, web_assets::kSetupHtml); }

// Captive portal: whatever a phone probes for through the hotspot is
// redirected to the setup page, which makes the phone open it.
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
    cJSON_AddNumberToObject(r, "uptime", now / 1000);
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
      cJSON_AddNumberToObject(sensor, "measuringFor", (now - s.measuringSinceMs) / 1000);
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

    if (!ctx.settings->batteryInstalled || std::isnan(s.batteryVolts)) {
      cJSON_AddNullToObject(r, "battery");
    } else {
      cJSON* battery = cJSON_AddObjectToObject(r, "battery");
      addNumber(battery, "volts", s.batteryVolts, 2);
      addNumber(battery, "percent", s.batteryPercent, 0);
    }

    cJSON* wifi = cJSON_AddObjectToObject(r, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", ctx.wifi->ssid());
    cJSON_AddNumberToObject(wifi, "rssi", ctx.wifi->rssi());
    addMqttState(cJSON_AddObjectToObject(r, "mqtt"), *ctx.mqtt);
  }
  return sendJson(req, r);
}

esp_err_t getHistory(httpd_req_t* req) {
  char query[64] = "";
  char id[8] = "";
  char range[8] = "hour";
  httpd_req_get_url_query_str(req, query, sizeof query);
  httpd_query_key_value(query, "metric", id, sizeof id);
  httpd_query_key_value(query, "range", range, sizeof range);
  Metric metric;
  if (!metricFromId(id, metric)) return badRequest(req, "Unknown metric");
  const bool day = strcmp(range, "day") == 0;
  const size_t capacity = day ? aq::MetricHistory::Coarse::capacity()
                              : aq::MetricHistory::Fine::capacity();
  const uint32_t step = (day ? aq::MetricHistory::kCoarsePeriodMs : aq::MetricHistory::kFinePeriodMs) / 1000;

  // Copy under the lock, then send without holding it (the client may be slow).
  std::unique_ptr<float[]> values(new (std::nothrow) float[capacity]);
  if (!values) return httpd_resp_send_500(req);
  size_t count = 0;
  {
    AppLock lock;
    auto copy = [&](const auto& ring) {
      count = ring.size();
      for (size_t i = 0; i < count; ++i) values[i] = ring[i];
    };
    if (day) {
      copy(ctx.history->coarse(metric));
    } else {
      copy(ctx.history->fine(metric));
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

// ---------------------------------------------------------------- settings

void addSettings(cJSON* r, const Settings& s) {
  cJSON_AddStringToObject(r, "name", s.name);
  cJSON_AddNumberToObject(r, "brightness", s.brightness);
  cJSON_AddNumberToObject(r, "dimAfter", s.dimAfterS);
  cJSON_AddBoolToObject(r, "fahrenheit", s.fahrenheit);
  cJSON_AddStringToObject(r, "led", ledId(s.led));
  addNumber(r, "tempOffset", s.tempOffsetC, 1);
  cJSON_AddNumberToObject(r, "altitude", s.altitudeM);
  cJSON_AddBoolToObject(r, "co2SelfCalibration", s.co2SelfCalibration);
  cJSON_AddBoolToObject(r, "batteryInstalled", s.batteryInstalled);
  cJSON_AddBoolToObject(r, "wifiEnabled", s.wifiEnabled);
  cJSON_AddBoolToObject(r, "passwordSet", s.webPassword[0] != '\0');
}

esp_err_t getSettings(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  {
    AppLock lock;
    addSettings(root.get(), *ctx.settings);
  }
  return sendJson(req, root.get());
}

bool printable(const char* text) {
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c < 0x20 || *c > 0x7e) return false;
  }
  return true;
}

// Applies the fields present in `json` to `s`; returns what's wrong, if anything.
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
  if ((f = field(json, "batteryInstalled"))) {
    if (!cJSON_IsBool(f)) return "Battery: true or false";
    s.batteryInstalled = cJSON_IsTrue(f);
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

esp_err_t postSettings(httpd_req_t* req) {
  if (!authorized(req)) return askForPassword(req);
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

esp_err_t postAction(httpd_req_t* req) {
  if (!authorized(req)) return askForPassword(req);
  const Json json = readJson(req);
  const char* action = json ? stringField(json.get(), "action") : nullptr;
  if (action == nullptr) return badRequest(req, "Expected {\"action\": ...}");
  if (strcmp(action, "cleanFan") == 0) {
    ctx.sensor->cleanFan();
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
    const String host = String(device::hostname()) + ".local";
    cJSON_AddStringToObject(r, "host", host.c_str());

    cJSON* saved = cJSON_AddArrayToObject(r, "saved");
    for (size_t i = 0; i < w.saved().size(); ++i) {
      cJSON* item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "ssid", w.saved()[i].ssid);
      cJSON_AddBoolToObject(item, "hidden", w.saved()[i].hidden);
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

// Network name, password and hidden flag from a request.
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

// Runs a POST /api/wifi action under AppLock; returns what's wrong, if anything.
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
  } else if (strcmp(action, "finish") == 0) {
    w.stopSetupSoon();
  } else {
    return "Unknown action";
  }
  return nullptr;
}

esp_err_t postWifi(httpd_req_t* req) {
  if (!authorized(req)) return askForPassword(req);
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

void addMqtt(cJSON* r, const Settings& s, const MqttService& mqtt) {
  cJSON_AddBoolToObject(r, "enabled", s.mqttEnabled);
  cJSON_AddStringToObject(r, "host", s.mqttHost);
  cJSON_AddNumberToObject(r, "port", s.mqttPort);
  cJSON_AddStringToObject(r, "user", s.mqttUser);
  cJSON_AddBoolToObject(r, "passwordSet", s.mqttPassword[0] != '\0');
  addMqttState(r, mqtt);
}

esp_err_t getMqtt(httpd_req_t* req) {
  Json root(cJSON_CreateObject());
  {
    AppLock lock;
    addMqtt(root.get(), *ctx.settings, *ctx.mqtt);
  }
  return sendJson(req, root.get());
}

// Applies the fields present in `json` to `s`; returns what's wrong, if anything.
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

esp_err_t postMqtt(httpd_req_t* req) {
  if (!authorized(req)) return askForPassword(req);
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

esp_err_t getAuth(httpd_req_t* req) {
  return authorized(req) ? sendOk(req) : askForPassword(req);
}

esp_err_t postOta(httpd_req_t* req) {
  if (!authorized(req)) return askForPassword(req);
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
  sys::restartLater(2000);
  return sendText(req, "200 OK", "Update stored; the monitor restarts now.");
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
      {"/api/settings", HTTP_GET, getSettings, nullptr},
      {"/api/settings", HTTP_POST, postSettings, nullptr},
      {"/api/action", HTTP_POST, postAction, nullptr},
      {"/api/wifi", HTTP_GET, getWifi, nullptr},
      {"/api/wifi", HTTP_POST, postWifi, nullptr},
      {"/api/mqtt", HTTP_GET, getMqtt, nullptr},
      {"/api/mqtt", HTTP_POST, postMqtt, nullptr},
      {"/api/auth", HTTP_GET, getAuth, nullptr},
      {"/api/ota", HTTP_POST, postOta, nullptr},
  };
  for (const httpd_uri_t& route : routes) httpd_register_uri_handler(server, &route);
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notFound);
  Serial.printf("Web: http://%s.local/\n", device::hostname());
  return true;
}

bool started() { return server != nullptr; }

}  // namespace web
