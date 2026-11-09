// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Unit tests for the hardware-independent logic in lib/aqcore,
/// lib/netcore and lib/powercore.
///
/// Run on the board with `pio test`.

#include <AirQuality.h>
#include <BatteryWatch.h>
#include <ButtonLogic.h>
#include <Comfort.h>
#include <History.h>
#include <Ipv4.h>
#include <LoginGuard.h>
#include <WifiChoice.h>
#include <unity.h>

#include <cmath>
#include <memory>

using namespace aq;

/// Unity's set-up before each test: nothing to do.
void setUp() {}
/// Unity's clean-up after each test: nothing to do.
void tearDown() {}

/// evaluate() gives Unknown for a NaN value.
static void test_level_unknown_for_nan() {
  TEST_ASSERT_EQUAL(Level::Unknown, evaluate(NAN, defaultThresholds(Metric::Co2), Level::Good));
}

/// An upper limit: levels rise at the limit and fall back past the hysteresis.
static void test_level_high_side_with_hysteresis() {
  const Thresholds& co2 = defaultThresholds(Metric::Co2);  // 950 / 2500, hysteresis 50
  TEST_ASSERT_EQUAL(Level::Good, evaluate(949, co2, Level::Good));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(950, co2, Level::Good));
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(2500, co2, Level::Warning));
  // Falling back just below a limit keeps the level until past the hysteresis.
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(2460, co2, Level::Alarm));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(2449, co2, Level::Alarm));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(910, co2, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Good, evaluate(900, co2, Level::Warning));
  // Rising doesn't use hysteresis.
  TEST_ASSERT_EQUAL(Level::Good, evaluate(940, co2, Level::Good));
}

/// Limits on both sides: each side has its own hysteresis band.
static void test_level_two_sided() {
  const Thresholds& rh = defaultThresholds(Metric::Humidity);  // 20/30 .. 60/70, hysteresis 2
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(19, rh, Level::Good));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(25, rh, Level::Good));
  TEST_ASSERT_EQUAL(Level::Good, evaluate(45, rh, Level::Good));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(65, rh, Level::Good));
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(70, rh, Level::Good));
  // Each side has its own hysteresis band.
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(31, rh, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Good, evaluate(32, rh, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(59, rh, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Good, evaluate(58, rh, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(21, rh, Level::Alarm));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(22, rh, Level::Alarm));
}

/// The overall level is the worst among metrics with limits; kPriority breaks ties.
static void test_overall_is_worst_and_ties_follow_priority() {
  AirQuality aq;
  Measurement m;
  aq.update(m);
  TEST_ASSERT_EQUAL(Level::Unknown, aq.overall());

  m[Metric::Co2] = 600;
  m[Metric::Pm25] = 20;  // warning
  m[Metric::Pm1] = 500;  // no limits: never drives the overall level
  aq.update(m);
  TEST_ASSERT_EQUAL(Level::Warning, aq.overall());
  TEST_ASSERT_EQUAL(Metric::Pm25, aq.worst());
  TEST_ASSERT_EQUAL(Level::Good, aq.level(Metric::Pm1));

  m[Metric::Co2] = 1100;  // also a warning; CO2 has priority over PM2.5
  aq.update(m);
  TEST_ASSERT_EQUAL(Metric::Co2, aq.worst());

  m[Metric::Hcho] = 90;  // alarm beats warnings
  aq.update(m);
  TEST_ASSERT_EQUAL(Level::Alarm, aq.overall());
  TEST_ASSERT_EQUAL(Metric::Hcho, aq.worst());
}

/// A full Ring keeps the newest samples, oldest first.
static void test_ring_keeps_newest_in_order() {
  Ring<3> r;
  TEST_ASSERT_EQUAL(0, r.size());
  for (int i = 1; i <= 5; ++i) r.push(static_cast<float>(i));
  TEST_ASSERT_EQUAL(3, r.size());
  TEST_ASSERT_EQUAL_FLOAT(3, r[0]);
  TEST_ASSERT_EQUAL_FLOAT(5, r[2]);
}

/// groupMeans() lines the groups up with the newest sample and skips gaps.
static void test_group_means_line_up_with_the_newest() {
  Ring<8> r;
  for (int i = 1; i <= 7; ++i) r.push(static_cast<float>(i));
  float out[8];
  // The newest 6 in threes: (2 3 4) (5 6 7).
  TEST_ASSERT_EQUAL(2, groupMeans(r, 6, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(3, out[0]);
  TEST_ASSERT_EQUAL_FLOAT(6, out[1]);
  // More than there is: only the oldest group is short, (1) (2 3 4) (5 6 7).
  TEST_ASSERT_EQUAL(3, groupMeans(r, 100, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(1, out[0]);
  TEST_ASSERT_EQUAL_FLOAT(6, out[2]);
  // Gaps don't count; a group of nothing but gaps stays one.
  r.push(NAN);
  r.push(NAN);  // 2 3 4 5 6 7 - -
  TEST_ASSERT_EQUAL(4, groupMeans(r, 8, 2, out));
  TEST_ASSERT_EQUAL_FLOAT(2.5f, out[0]);
  TEST_ASSERT_TRUE(std::isnan(out[3]));
  TEST_ASSERT_EQUAL(1, groupMeans(r, 3, 3, out));
  TEST_ASSERT_EQUAL_FLOAT(7, out[0]);
}

/// MetricHistory keeps 10 s means, and NaN for periods without data.
static void test_history_averages_and_records_gaps() {
  auto h = std::make_unique<MetricHistory>();
  Measurement m;
  uint32_t t = 1000;
  for (int i = 0; i < 10; ++i, t += 1000) {
    m[Metric::Co2] = 400.0f + i;  // mean 404.5 over the first 10 s
    h->update(&m, t);
  }
  h->update(nullptr, t);  // closes the first 10 s period
  const auto& fine = h->fine(Metric::Co2);
  TEST_ASSERT_EQUAL(1, fine.size());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 404.5f, fine[0]);
  TEST_ASSERT_TRUE(std::isnan(h->fine(Metric::Hcho)[0]));

  t += 25000;  // no data for 25 s: two empty periods
  h->update(nullptr, t);
  TEST_ASSERT_EQUAL(3, fine.size());
  TEST_ASSERT_TRUE(std::isnan(fine[1]));
  TEST_ASSERT_TRUE(std::isnan(fine[2]));
}

/// ButtonLogic debounces and gives Press, Click, LongPress and Repeat.
static void test_button_click_long_press_and_repeat() {
  ButtonLogic b;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, t += 10));  // bounce
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t += 5));
  TEST_ASSERT_EQUAL(ButtonEvent::Press, b.update(true, t += 30));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, t += 100));
  TEST_ASSERT_EQUAL(ButtonEvent::Click, b.update(false, t += 30));

  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t += 1000));
  TEST_ASSERT_EQUAL(ButtonEvent::Press, b.update(true, t += 30));
  TEST_ASSERT_EQUAL(ButtonEvent::LongPress, b.update(true, t += ButtonLogic::kLongPressMs));
  TEST_ASSERT_EQUAL(ButtonEvent::Repeat, b.update(true, t += ButtonLogic::kRepeatMs));
  b.update(false, t += 10);
  // A long press doesn't also produce a click on release.
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, t += 30));
}

/// After ButtonLogic::swallow(), the rest of the press gives no events.
static void test_button_swallow_suppresses_rest_of_press() {
  ButtonLogic b;
  uint32_t t = 0;
  b.update(true, t);
  TEST_ASSERT_EQUAL(ButtonEvent::Press, b.update(true, t += 30));
  b.swallow();
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t += 2000));
  b.update(false, t += 10);
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, t += 30));
  // The next press works normally again.
  b.update(true, t += 100);
  b.update(true, t += 30);
  b.update(false, t += 50);
  TEST_ASSERT_EQUAL(ButtonEvent::Click, b.update(false, t += 30));
}

/// ButtonLogic::ignoreHeld() ignores the press under way, not the next one.
static void test_button_ignore_held_until_released() {
  ButtonLogic b;
  uint32_t t = 0;
  b.ignoreHeld();
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t += 30));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, t += 2000));  // no long press
  b.update(false, t += 10);
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, t += 30));  // no click
  b.update(true, t += 100);
  TEST_ASSERT_EQUAL(ButtonEvent::Press, b.update(true, t += 30));
}

/// Dew point, absolute humidity and degF at 25 degC, 50 %RH; NaN gives NaN.
static void test_comfort_values() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 13.9f, dewPoint(25, 50));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 11.5f, absoluteHumidity(25, 50));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 77.0f, toFahrenheit(25));
  TEST_ASSERT_TRUE(std::isnan(dewPoint(NAN, 50)));
}

/// chooseNetworks(): networks seen by signal, then hidden ones not seen.
static void test_wifi_choice_strongest_first_then_hidden() {
  const net::SavedNetwork saved[] = {
      {"Home", false}, {"Office", false}, {"Lab", true}, {"Cabin", false}, {"Attic", true}};
  const net::ScanEntry scan[] = {
      {"Neighbour", -40}, {"Office", -70}, {"Home", -80}, {"Home", -55}, {"", -60}, {"Attic", -75}};
  net::Candidate out[5];
  const size_t n = net::chooseNetworks(saved, 5, scan, 6, out);
  TEST_ASSERT_EQUAL(4, n);
  TEST_ASSERT_EQUAL(0, out[0].saved);  // Home, through its stronger access point
  TEST_ASSERT_EQUAL(3, out[0].scanIndex);
  TEST_ASSERT_EQUAL(1, out[1].saved);  // Office
  TEST_ASSERT_EQUAL(4, out[2].saved);  // Attic: hidden but seen, so ranked by signal
  TEST_ASSERT_EQUAL(5, out[2].scanIndex);
  TEST_ASSERT_EQUAL(2, out[3].saved);  // Lab: hidden and not seen, tried last
  TEST_ASSERT_EQUAL(-1, out[3].scanIndex);
  // Cabin isn't in range and isn't hidden: not tried.
}

/// chooseNetworks(): equal signals keep the saved order; nothing seen, nothing tried.
static void test_wifi_choice_ties_keep_saved_order() {
  const net::SavedNetwork saved[] = {{"A", false}, {"B", false}};
  const net::ScanEntry scan[] = {{"B", -60}, {"A", -60}};
  net::Candidate out[2];
  TEST_ASSERT_EQUAL(2, net::chooseNetworks(saved, 2, scan, 2, out));
  TEST_ASSERT_EQUAL(0, out[0].saved);
  TEST_ASSERT_EQUAL(1, out[1].saved);
  TEST_ASSERT_EQUAL(0, net::chooseNetworks(saved, 2, scan, 0, out));
}

/// LoginGuard: free tries, then waits that double up to the limit.
static void test_login_guard_waits_double_up_to_a_limit() {
  net::LoginGuard g;
  uint32_t t = 1000;
  g.failed(t);
  g.failed(t);
  TEST_ASSERT_EQUAL(0, g.waitMs(t));  // two misses are free
  g.failed(t);
  TEST_ASSERT_EQUAL(60000, g.waitMs(t));
  TEST_ASSERT_EQUAL(1000, g.waitMs(t + 59000));
  TEST_ASSERT_EQUAL(0, g.waitMs(t += 60000));
  g.failed(t);
  TEST_ASSERT_EQUAL(120000, g.waitMs(t));
  for (int i = 0; i < 10; ++i) g.failed(t += 15 * 60000);
  TEST_ASSERT_EQUAL(net::LoginGuard::kMaxWaitMs, g.waitMs(t));
  TEST_ASSERT_EQUAL(14, g.failures());
  // The right password resets the wait but not the count.
  g.succeeded();
  TEST_ASSERT_EQUAL(0, g.waitMs(t));
  g.failed(t);
  g.failed(t);
  TEST_ASSERT_EQUAL(0, g.waitMs(t));
  TEST_ASSERT_EQUAL(16, g.failures());
}

/// LoginGuard's wait ends for good, also past where millis() would wrap.
static void test_login_guard_wait_ends_for_good() {
  net::LoginGuard g;
  const uint64_t t = 0xFFFFFFFFull - 30000;
  for (int i = 0; i < 3; ++i) g.failed(t);
  TEST_ASSERT_EQUAL(30000, g.waitMs(t + 30000));  // past 2^32 ms
  TEST_ASSERT_EQUAL(0, g.waitMs(t + 60000));
  TEST_ASSERT_EQUAL(0, g.waitMs(t + 0x100000000ull));  // 49.7 days later
  TEST_ASSERT_EQUAL(0, g.waitMs(t + 0x100000000ull + 1000));
}

/// parseIpv4() takes exactly `a.b.c.d`; formatIpv4() writes it.
static void test_ipv4_parse_and_format() {
  uint32_t a = 0;
  TEST_ASSERT_TRUE(net::parseIpv4("192.168.1.20", a));
  TEST_ASSERT_EQUAL_HEX32(0xC0A80114, a);
  TEST_ASSERT_TRUE(net::parseIpv4("0.0.0.0", a));
  TEST_ASSERT_EQUAL_HEX32(0, a);
  for (const char* bad : {"", "192.168.1", "192.168.1.256", "192.168.1.20.", "1.2.3.4x", " 1.2.3.4",
                          "1..2.3", "1.2.3.0004", "-1.2.3.4"}) {
    TEST_ASSERT_FALSE_MESSAGE(net::parseIpv4(bad, a), bad);
  }
  char text[16];
  net::formatIpv4(0xC0A80114, text, sizeof text);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", text);
}

/// checkFixedAddress() turns down bad masks, addresses, gateways and DNS servers.
static void test_fixed_address_checks() {
  uint32_t ip, mask, gw, dns;
  net::parseIpv4("192.168.1.20", ip);
  net::parseIpv4("255.255.255.0", mask);
  net::parseIpv4("192.168.1.1", gw);
  net::parseIpv4("1.1.1.1", dns);
  TEST_ASSERT_NULL(net::checkFixedAddress(ip, mask, gw, 0));
  TEST_ASSERT_NULL(net::checkFixedAddress(ip, mask, gw, dns));
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, 0xFFFF00FF, gw, 0));  // gaps in the mask
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, 0xFFFFFFFF, gw, 0));  // no room
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, 0x00FFFFFF, gw, 0));
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(0xC0A80100, mask, gw, 0));  // network address
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(0xC0A801FF, mask, gw, 0));  // broadcast
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, mask, 0xC0A80201, 0));  // gateway elsewhere
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, mask, ip, 0));
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(0x7F000001, 0xFF000000, 0x7F000002, 0));  // loopback
  TEST_ASSERT_NOT_NULL(net::checkFixedAddress(ip, mask, gw, 0xE0000001));  // multicast DNS server
}

/// power::stateOf(): the battery's state from the charge rate.
static void test_battery_state_from_charge_rate() {
  TEST_ASSERT_EQUAL(power::BatteryState::Unknown, power::stateOf(NAN));
  TEST_ASSERT_EQUAL(power::BatteryState::Charging, power::stateOf(12.5f));
  TEST_ASSERT_EQUAL(power::BatteryState::Discharging, power::stateOf(-6.2f));
  TEST_ASSERT_EQUAL(power::BatteryState::Idle, power::stateOf(0.208f));
  TEST_ASSERT_EQUAL(power::BatteryState::Idle, power::stateOf(-0.208f));
}

/// Voltage jumps set the state until the rate follows; Wi-Fi dips don't count.
static void test_battery_state_follows_voltage_jumps() {
  using power::BatteryState;
  power::BatteryWatch w;
  uint32_t t = 0;
  w.update(3.85f, 40, -60, t);
  TEST_ASSERT_EQUAL(BatteryState::Discharging, w.state());
  w.update(4.02f, 40, -58, t += 5000);  // plugged in; the rate hasn't followed yet
  TEST_ASSERT_EQUAL(BatteryState::Charging, w.state());
  w.update(4.03f, 40, -20, t += 60000);
  TEST_ASSERT_EQUAL(BatteryState::Charging, w.state());
  w.update(4.03f, 41, 30, t += 30000);  // it has
  TEST_ASSERT_EQUAL(BatteryState::Charging, w.state());
  w.update(3.86f, 41, 25, t += 5000);  // unplugged
  TEST_ASSERT_EQUAL(BatteryState::Discharging, w.state());
  w.update(3.85f, 41, 10, t += 60000);
  TEST_ASSERT_EQUAL(BatteryState::Discharging, w.state());

  power::BatteryWatch dip;  // Wi-Fi traffic on battery: not a plug
  dip.update(3.70f, 50, -60, 0);
  dip.update(3.655f, 50, -60, 5000);
  dip.update(3.70f, 50, -60, 10000);
  TEST_ASSERT_EQUAL(BatteryState::Discharging, dip.state());

  power::BatteryWatch slow;  // the rate never follows: the jump stops counting
  slow.update(3.85f, 40, -60, 0);
  slow.update(4.02f, 40, -60, 5000);
  TEST_ASSERT_EQUAL(BatteryState::Charging, slow.state());
  slow.update(4.02f, 40, -60, 5000 + power::BatteryWatch::kSettleMs);
  TEST_ASSERT_EQUAL(BatteryState::Discharging, slow.state());
}

/// A full battery on USB power isn't discharging, though the rate wanders below 0.
static void test_battery_full_on_usb_is_not_in_use() {
  using power::BatteryState;
  power::BatteryWatch w;
  w.update(4.155f, 96, -4.4f, 0);  // the rate wanders while it's full
  TEST_ASSERT_EQUAL(BatteryState::Idle, w.state());
  w.update(4.156f, 96, 4.6f, 5000);
  TEST_ASSERT_EQUAL(BatteryState::Charging, w.state());
  w.update(4.055f, 95, -4.2f, 10000);  // unplugged
  TEST_ASSERT_EQUAL(BatteryState::Discharging, w.state());
}

/// After pluggedIn(), it's charging without a warning while the rate catches up.
static void test_battery_started_on_usb_power() {
  // Started again by USB power after an empty battery: the rate still says
  // discharging, and the charge is below the first warning's limit.
  power::BatteryWatch w;
  w.setPresent(true);
  w.pluggedIn(0);
  w.update(3.84f, 19.6f, -8.5f, 1000);
  TEST_ASSERT_EQUAL(power::BatteryState::Charging, w.state());
  TEST_ASSERT_EQUAL(0, w.warning());
  w.update(3.85f, 20.1f, 3.7f, 61000);
  TEST_ASSERT_EQUAL(power::BatteryState::Charging, w.state());
}

/// Warnings at 20 % and 10 % charge, ending only clearly above the limit.
static void test_battery_warnings_with_hysteresis() {
  power::BatteryWatch w;
  w.setPresent(true);
  w.update(3.7f, 21, -8, 0);
  TEST_ASSERT_EQUAL(0, w.warning());
  w.update(3.7f, 20, -8, 0);
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(3.7f, 21.5f, -8, 0);  // wavering just above the limit
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(3.6f, 9.5f, -8, 0);
  TEST_ASSERT_EQUAL(2, w.warning());
  w.update(3.6f, 11, -8, 0);
  TEST_ASSERT_EQUAL(2, w.warning());
  w.update(3.6f, 12.5f, -8, 0);  // clearly above 10 %, still below 20 %
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(3.6f, 12, 15, 0);  // USB power: charging
  TEST_ASSERT_EQUAL(0, w.warning());
  w.update(NAN, NAN, NAN, 0);
  TEST_ASSERT_EQUAL(0, w.warning());
}

/// Warnings by voltage: a minute at 3.6 V or below, then 3.55 V, at any charge.
static void test_battery_warns_by_voltage() {
  // A 400 mAh battery running the monitor: 3.6 V while the gauge read 45 %.
  power::BatteryWatch w;
  w.setPresent(true);
  uint32_t t = 0;
  w.update(3.61f, 46, -75, t);
  TEST_ASSERT_EQUAL(0, w.warning());
  w.update(3.60f, 45, -75, t += 5000);
  w.update(3.59f, 45, -75, t += 55000);
  TEST_ASSERT_EQUAL_MESSAGE(0, w.warning(), "55 s low");
  w.update(3.59f, 45, -75, t += 5000);
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(3.62f, 44, -75, t += 5000);  // a lighter load lifts it for a moment
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(3.55f, 34, -75, t += 5000);
  w.update(3.55f, 34, -75, t += 60000);
  TEST_ASSERT_EQUAL(2, w.warning());
  w.update(3.85f, 34, -60, t += 5000);  // plugged in
  TEST_ASSERT_EQUAL(0, w.warning());
}

/// Learning where "empty" is, and percent() counting the charge left down to it.
static void test_battery_counts_down_to_switching_off() {
  power::BatteryWatch w;
  w.setPresent(true);
  w.update(3.9f, 60, -75, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60, w.percent());  // nothing learned: the gauge's own
  w.update(3.49f, 20.5f, -80, 5000);  // switching itself off
  TEST_ASSERT_TRUE(w.learnEmpty());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.5f, w.emptyPercent());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0, w.percent());
  TEST_ASSERT_FALSE_MESSAGE(w.learnEmpty(), "unchanged");
  w.update(3.9f, 60.25f, -75, 10000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50, w.percent());
  w.update(3.7f, 36, -75, 15000);  // 19.5 % left: the warnings count what's left
  TEST_ASSERT_EQUAL(1, w.warning());
  w.update(4.15f, 100, 0, 20000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100, w.percent());

  power::BatteryWatch off;
  off.setEmptyPercent(20.5f);
  off.update(3.49f, 55, -80, 0);  // a reading that's off isn't learned
  TEST_ASSERT_FALSE(off.learnEmpty());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.5f, off.emptyPercent());
  off.setEmptyPercent(NAN);  // never learned
  TEST_ASSERT_EQUAL_FLOAT(0, off.emptyPercent());
  off.setEmptyPercent(75);
  TEST_ASSERT_EQUAL_FLOAT(0, off.emptyPercent());
  TEST_ASSERT_TRUE(std::isnan(power::BatteryWatch().percent()));
}

/// empty() after a minute at 3.5 V or below while discharging, and not otherwise.
static void test_battery_empty_after_a_minute_low() {
  power::BatteryWatch w;
  w.setPresent(true);
  uint32_t t = 1000;
  w.update(3.49f, 4, -8, t);
  TEST_ASSERT_FALSE_MESSAGE(w.empty(), "just went low");
  w.update(3.52f, 4, -8, t += 30000);  // a dip, not yet empty
  w.update(3.49f, 4, -8, t += 1000);
  w.update(3.48f, 3, -8, t += 59000);
  TEST_ASSERT_FALSE_MESSAGE(w.empty(), "59 s low");
  w.update(3.48f, 3, -8, t += 1000);
  TEST_ASSERT_TRUE(w.empty());
  w.update(3.48f, 3, 20, t += 1000);  // USB power came back
  TEST_ASSERT_FALSE_MESSAGE(w.empty(), "charging");
  power::BatteryWatch idle;
  idle.setPresent(true);
  idle.update(3.3f, 0, 0, 0);
  idle.update(3.3f, 0, 0, 120000);
  TEST_ASSERT_FALSE_MESSAGE(idle.empty(), "not discharging");
}

/// @brief What the battery gauge reads without a battery: the charger, in V
/// (recorded, 5 s apart).
static const float kCharger[] = {4.107f, 4.072f, 4.061f, 4.183f, 4.144f, 4.149f, 4.106f, 4.124f,
                                 4.070f, 4.110f, 4.135f, 4.130f, 4.141f, 4.081f, 4.059f, 4.183f,
                                 4.110f, 4.107f, 4.140f, 4.146f, 4.099f, 4.104f, 4.112f, 4.071f};

/// present() from the voltage: the charger alone jitters, a battery doesn't.
static void test_battery_presence_from_the_voltage() {
  power::BatteryWatch w;
  w.setPresent(true);  // as before a restart
  uint32_t t = 0;
  for (float v : kCharger) w.update(v, 98, -20, t += 5000);
  TEST_ASSERT_FALSE(w.present());
  TEST_ASSERT_EQUAL(0, w.warning());
  for (int i = 0; i < 24; ++i) w.update(4.11f + i * 0.001f, 98, -20, t += 5000);  // connected
  TEST_ASSERT_TRUE(w.present());

  power::BatteryWatch busy;  // running the monitor, with Wi-Fi traffic
  for (int i = 0; i < 24; ++i) busy.update(i % 2 ? 3.55f : 3.58f, 30, -75, i * 5000);
  TEST_ASSERT_TRUE(busy.present());

  power::BatteryWatch early;  // too few readings to tell
  early.setPresent(true);
  for (int i = 0; i < 23; ++i) early.update(kCharger[i], 98, -20, i * 5000);
  TEST_ASSERT_TRUE(early.present());
}

/// takeConnected() once when a battery appears, which forgets where "empty" was.
static void test_battery_connected_while_running() {
  power::BatteryWatch w;
  w.setEmptyPercent(20.5f);
  uint32_t t = 0;
  for (int pass = 0; pass < 2; ++pass) {  // no battery for 4 minutes
    for (float v : kCharger) w.update(v, 98, -20, t += 5000);
  }
  TEST_ASSERT_FALSE(w.takeConnected());
  for (int i = 0; i < 24; ++i) w.update(3.90f + i * 0.001f, 98, -20, t += 5000);
  TEST_ASSERT_TRUE(w.present());
  TEST_ASSERT_TRUE(w.takeConnected());
  TEST_ASSERT_FALSE_MESSAGE(w.takeConnected(), "once");
  TEST_ASSERT_EQUAL_FLOAT(0, w.emptyPercent());

  power::BatteryWatch brief;  // no battery for a moment only
  brief.setEmptyPercent(20.5f);
  t = 0;
  for (float v : kCharger) brief.update(v, 98, -20, t += 5000);
  TEST_ASSERT_FALSE(brief.present());
  for (int i = 0; i < 24; ++i) brief.update(3.90f + i * 0.001f, 98, -20, t += 5000);
  TEST_ASSERT_TRUE(brief.takeConnected());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.5f, brief.emptyPercent());

  power::BatteryWatch all;  // a battery all along, though not known at the start
  all.setEmptyPercent(20.5f);
  for (int i = 0; i < 24; ++i) all.update(3.90f - i * 0.001f, 50, -75, i * 5000);
  TEST_ASSERT_TRUE(all.present());
  TEST_ASSERT_FALSE(all.takeConnected());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.5f, all.emptyPercent());
}

/// A load ending lifts the voltage like USB power; it doesn't put off switching off.
static void test_battery_empty_despite_a_jump_up() {
  power::BatteryWatch w;
  w.setPresent(true);
  uint32_t t = 0;
  for (int i = 0; i < 6; ++i) w.update(3.42f, 15, -9, t += 5000);  // under a heavy load
  w.update(3.49f, 15, -9, t += 5000);  // the load ends: +70 mV
  TEST_ASSERT_EQUAL(static_cast<int>(power::BatteryState::Charging), static_cast<int>(w.state()));
  for (int i = 0; i < 13; ++i) w.update(3.49f, 15, -9, t += 5000);  // 65 s
  TEST_ASSERT_TRUE(w.empty());
}

/// power::powerIsBack(): charging, or at least 3.8 V.
static void test_power_is_back() {
  TEST_ASSERT_TRUE(power::powerIsBack(3.55f, 9.8f));    // charging
  TEST_ASSERT_TRUE(power::powerIsBack(3.85f, 0));        // well charged
  TEST_ASSERT_FALSE_MESSAGE(power::powerIsBack(3.62f, 0), "resting after the shutdown");
  TEST_ASSERT_FALSE_MESSAGE(power::powerIsBack(3.5f, -0.2f), "asleep, on battery");
  TEST_ASSERT_FALSE_MESSAGE(power::powerIsBack(NAN, NAN), "no reading");
}

/// @brief Runs every test.
/// @return The number of failures.
static int runTests() {
  UNITY_BEGIN();
  RUN_TEST(test_level_unknown_for_nan);
  RUN_TEST(test_level_high_side_with_hysteresis);
  RUN_TEST(test_level_two_sided);
  RUN_TEST(test_overall_is_worst_and_ties_follow_priority);
  RUN_TEST(test_ring_keeps_newest_in_order);
  RUN_TEST(test_group_means_line_up_with_the_newest);
  RUN_TEST(test_history_averages_and_records_gaps);
  RUN_TEST(test_button_click_long_press_and_repeat);
  RUN_TEST(test_button_swallow_suppresses_rest_of_press);
  RUN_TEST(test_button_ignore_held_until_released);
  RUN_TEST(test_comfort_values);
  RUN_TEST(test_wifi_choice_strongest_first_then_hidden);
  RUN_TEST(test_wifi_choice_ties_keep_saved_order);
  RUN_TEST(test_login_guard_waits_double_up_to_a_limit);
  RUN_TEST(test_login_guard_wait_ends_for_good);
  RUN_TEST(test_ipv4_parse_and_format);
  RUN_TEST(test_fixed_address_checks);
  RUN_TEST(test_battery_state_from_charge_rate);
  RUN_TEST(test_battery_state_follows_voltage_jumps);
  RUN_TEST(test_battery_full_on_usb_is_not_in_use);
  RUN_TEST(test_battery_started_on_usb_power);
  RUN_TEST(test_battery_warnings_with_hysteresis);
  RUN_TEST(test_battery_warns_by_voltage);
  RUN_TEST(test_battery_counts_down_to_switching_off);
  RUN_TEST(test_battery_empty_after_a_minute_low);
  RUN_TEST(test_battery_empty_despite_a_jump_up);
  RUN_TEST(test_battery_presence_from_the_voltage);
  RUN_TEST(test_battery_connected_while_running);
  RUN_TEST(test_power_is_back);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
/// On the board: waits for the test runner, then runs the tests.
void setup() {
  // With native USB the port only appears after the board has rebooted, so
  // wait for the test runner to open it before printing anything.
  for (uint32_t start = millis(); !Serial && millis() - start < 15000;) delay(10);
  delay(500);
  runTests();
}
/// On the board: nothing more; the tests ran in setup().
void loop() {}
#else
/// @brief Off the board: runs the tests.
/// @return The number of failures.
int main() { return runTests(); }
#endif
