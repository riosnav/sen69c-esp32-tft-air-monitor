// SPDX-License-Identifier: GPL-3.0-or-later
//
// Arduino's String, as far as the firmware's UI uses it. Numbers are
// formatted the way Arduino-ESP32 formats them, padding included:
// String(5.0f, 0) is " 5".
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace arduino_detail {

// Arduino-ESP32's dtostrf (cores/esp32/stdlib_noniso.c), step for step.
inline std::string dtostrf(double number, int width, unsigned int prec) {
  if (std::isnan(number)) return "nan";
  if (std::isinf(number)) return "inf";
  std::string out;
  bool negative = false;
  int fillme = width;  // cells for the integer part
  if (prec > 0) fillme -= static_cast<int>(prec) + 1;
  if (number < 0.0) {
    negative = true;
    fillme--;
    number = -number;
  }
  double rounding = 2.0;
  for (unsigned int i = 0; i < prec; ++i) rounding *= 10.0;
  number += 1.0 / rounding;
  double tenpow = 1.0;
  unsigned int digitcount = 1;
  while (number >= 10.0 * tenpow) {
    tenpow *= 10.0;
    digitcount++;
  }
  number /= tenpow;
  fillme -= static_cast<int>(digitcount);
  while (fillme-- > 0) out += ' ';
  if (negative) out += '-';
  digitcount += prec;
  while (digitcount-- > 0) {
    int digit = static_cast<int8_t>(number);
    if (digit > 9) digit = 9;
    out += static_cast<char>('0' | digit);
    if (digitcount == prec && prec > 0) out += '.';
    number -= digit;
    number *= 10.0;
  }
  return out;
}

template <typename T>
std::string integer(T value, unsigned int base) {
  if (base < 2 || base > 36) base = 10;
  using U = std::make_unsigned_t<T>;
  const bool negative = std::is_signed_v<T> && value < 0;
  U u = negative ? static_cast<U>(U(0) - static_cast<U>(value)) : static_cast<U>(value);
  std::string s;
  do {
    const int d = static_cast<int>(u % base);
    s.insert(s.begin(), static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10));
    u /= base;
  } while (u != 0);
  if (negative) s.insert(s.begin(), '-');
  return s;
}

// Numbers that String's operators turn into digits (char is a character).
template <typename T>
constexpr bool kIsNumber =
    std::is_arithmetic_v<T> && !std::is_same_v<T, char> && !std::is_same_v<T, bool>;

}  // namespace arduino_detail

class String {
 public:
  String(const char* text = "") : s_(text ? text : "") {}
  String(std::string text) : s_(std::move(text)) {}
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(int v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(unsigned int v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(unsigned long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(long long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  explicit String(unsigned long long v, unsigned char base = 10)
      : s_(arduino_detail::integer(v, base)) {}
  explicit String(float v, unsigned int decimals = 2)
      : s_(arduino_detail::dtostrf(v, static_cast<int>(decimals) + 2, decimals)) {}
  explicit String(double v, unsigned int decimals = 2)
      : s_(arduino_detail::dtostrf(v, static_cast<int>(decimals) + 2, decimals)) {}

  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }
  char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
  char charAt(unsigned int i) const { return (*this)[i]; }

  int indexOf(char c, unsigned int from = 0) const { return found(s_.find(c, from)); }
  int indexOf(const char* text, unsigned int from = 0) const { return found(s_.find(text, from)); }
  int lastIndexOf(char c) const { return found(s_.rfind(c)); }
  bool startsWith(const char* text) const { return s_.rfind(text, 0) == 0; }

  String substring(unsigned int from) const { return substring(from, length()); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = length();
    return String(s_.substr(from, to - from));
  }
  // Removes everything from `index` on, or `count` characters.
  void remove(unsigned int index) {
    if (index < s_.size()) s_.erase(index);
  }
  void remove(unsigned int index, unsigned int count) {
    if (index < s_.size()) s_.erase(index, count);
  }

  bool operator==(const String& other) const { return s_ == other.s_; }
  bool operator==(const char* other) const { return s_ == (other ? other : ""); }
  bool operator!=(const String& other) const { return !(*this == other); }
  bool operator!=(const char* other) const { return !(*this == other); }

  String& operator+=(const String& rhs) {
    s_ += rhs.s_;
    return *this;
  }
  String& operator+=(const char* rhs) {
    if (rhs) s_ += rhs;
    return *this;
  }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }
  template <typename T, typename = std::enable_if_t<arduino_detail::kIsNumber<T>>>
  String& operator+=(T v) {
    return *this += String(v);
  }

 private:
  static int found(std::string::size_type pos) {
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  std::string s_;
};

inline String operator+(String lhs, const String& rhs) { return lhs += rhs; }
inline String operator+(String lhs, const char* rhs) { return lhs += rhs; }
inline String operator+(const char* lhs, const String& rhs) { return String(lhs) += rhs; }
inline String operator+(String lhs, char c) { return lhs += c; }
template <typename T, typename = std::enable_if_t<arduino_detail::kIsNumber<T>>>
String operator+(String lhs, T v) {
  return lhs += v;
}
