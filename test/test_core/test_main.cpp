// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the hardware-independent logic in lib/aqcore and lib/netcore.
// Run on the board with `pio test`.

#include <AirQuality.h>
#include <ButtonLogic.h>
#include <Comfort.h>
#include <History.h>
#include <Ipv4.h>
#include <LoginGuard.h>
#include <WifiChoice.h>
#include <unity.h>

#include <memory>

using namespace aq;

void setUp() {}
void tearDown() {}

static void test_level_unknown_for_nan() {
  TEST_ASSERT_EQUAL(Level::Unknown, evaluate(NAN, defaultThresholds(Metric::Co2), Level::Good));
}

static void test_level_high_side_with_hysteresis() {
  const Thresholds& co2 = defaultThresholds(Metric::Co2);  // 1000 / 1500, hysteresis 50
  TEST_ASSERT_EQUAL(Level::Good, evaluate(999, co2, Level::Good));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(1000, co2, Level::Good));
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(1500, co2, Level::Warning));
  // Falling back just below a limit keeps the level until past the hysteresis.
  TEST_ASSERT_EQUAL(Level::Alarm, evaluate(1460, co2, Level::Alarm));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(1449, co2, Level::Alarm));
  TEST_ASSERT_EQUAL(Level::Warning, evaluate(960, co2, Level::Warning));
  TEST_ASSERT_EQUAL(Level::Good, evaluate(950, co2, Level::Warning));
  // Rising doesn't use hysteresis.
  TEST_ASSERT_EQUAL(Level::Good, evaluate(990, co2, Level::Good));
}

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

static void test_ring_keeps_newest_in_order() {
  Ring<3> r;
  TEST_ASSERT_EQUAL(0, r.size());
  for (int i = 1; i <= 5; ++i) r.push(static_cast<float>(i));
  TEST_ASSERT_EQUAL(3, r.size());
  TEST_ASSERT_EQUAL_FLOAT(3, r[0]);
  TEST_ASSERT_EQUAL_FLOAT(5, r[2]);
}

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

static void test_comfort_values() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 13.9f, dewPoint(25, 50));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 11.5f, absoluteHumidity(25, 50));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 77.0f, toFahrenheit(25));
  TEST_ASSERT_TRUE(std::isnan(dewPoint(NAN, 50)));
}

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

static void test_wifi_choice_ties_keep_saved_order() {
  const net::SavedNetwork saved[] = {{"A", false}, {"B", false}};
  const net::ScanEntry scan[] = {{"B", -60}, {"A", -60}};
  net::Candidate out[2];
  TEST_ASSERT_EQUAL(2, net::chooseNetworks(saved, 2, scan, 2, out));
  TEST_ASSERT_EQUAL(0, out[0].saved);
  TEST_ASSERT_EQUAL(1, out[1].saved);
  TEST_ASSERT_EQUAL(0, net::chooseNetworks(saved, 2, scan, 0, out));
}

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

static void test_login_guard_survives_millis_wrapping() {
  net::LoginGuard g;
  const uint32_t t = 0xFFFFFFFFu - 30000;
  for (int i = 0; i < 3; ++i) g.failed(t);
  TEST_ASSERT_EQUAL(30000, g.waitMs(t + 30000));  // past the wrap
  TEST_ASSERT_EQUAL(0, g.waitMs(t + 60000));
}

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

static int runTests() {
  UNITY_BEGIN();
  RUN_TEST(test_level_unknown_for_nan);
  RUN_TEST(test_level_high_side_with_hysteresis);
  RUN_TEST(test_level_two_sided);
  RUN_TEST(test_overall_is_worst_and_ties_follow_priority);
  RUN_TEST(test_ring_keeps_newest_in_order);
  RUN_TEST(test_history_averages_and_records_gaps);
  RUN_TEST(test_button_click_long_press_and_repeat);
  RUN_TEST(test_button_swallow_suppresses_rest_of_press);
  RUN_TEST(test_comfort_values);
  RUN_TEST(test_wifi_choice_strongest_first_then_hidden);
  RUN_TEST(test_wifi_choice_ties_keep_saved_order);
  RUN_TEST(test_login_guard_waits_double_up_to_a_limit);
  RUN_TEST(test_login_guard_survives_millis_wrapping);
  RUN_TEST(test_ipv4_parse_and_format);
  RUN_TEST(test_fixed_address_checks);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  // With native USB the port only appears after the board has rebooted, so
  // wait for the test runner to open it before printing anything.
  for (uint32_t start = millis(); !Serial && millis() - start < 15000;) delay(10);
  delay(500);
  runTests();
}
void loop() {}
#else
int main() { return runTests(); }
#endif
