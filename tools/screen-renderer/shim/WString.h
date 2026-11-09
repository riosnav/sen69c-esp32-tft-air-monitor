// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for Arduino's WString.h: String, as far as the firmware's
/// UI uses it.
///
/// Numbers are formatted the way Arduino-ESP32 formats them, padding
/// included: String(5.0f, 0) is " 5". The screens then show the same text as
/// the device's.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

/// How the String stand-in turns numbers into text, as Arduino-ESP32 does.
namespace arduino_detail {

/// @brief A number as text, as Arduino-ESP32's dtostrf
/// (`cores/esp32/stdlib_noniso.c`) writes it, step for step.
/// @param number The number.
/// @param width The least number of characters: spaces fill up on the left.
/// @param prec How many decimals.
/// @return The text; "nan" or "inf" (without a sign) for those.
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

/// @brief An integer's digits, with a minus sign if it's negative, as
/// Arduino-ESP32's String writes them.
///
/// Arduino-ESP32's gives "" for a base outside 2 to 16; this one takes 2 to
/// 36, and anything else as 10.
/// @tparam T The integer's type.
/// @param value The integer.
/// @param base The base; lowercase letters are the digits from 10 on.
/// @return The digits.
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

/// @brief Whether String's operators turn a `T` into digits: any arithmetic
/// type but char, which is a character, and bool.
template <typename T>
constexpr bool kIsNumber =
    std::is_arithmetic_v<T> && !std::is_same_v<T, char> && !std::is_same_v<T, bool>;

}  // namespace arduino_detail

/// @brief Arduino's String, on a std::string, as far as the firmware's UI uses
/// it.
///
/// Numbers become text as on Arduino-ESP32 (see arduino_detail::dtostrf() and
/// arduino_detail::integer()), so the screens show the same text.
class String {
 public:
  /// @brief A copy of `text`.
  /// @param text The text; nullptr counts as "".
  String(const char* text = "") : s_(text ? text : "") {}
  /// @brief Takes `text` over. Not in Arduino's String: the stand-in builds
  /// its results with it.
  /// @param text The text.
  String(std::string text) : s_(std::move(text)) {}
  /// @brief The single character `c`.
  /// @param c The character.
  explicit String(char c) : s_(1, c) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(unsigned char v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(int v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(unsigned int v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(unsigned long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(long long v, unsigned char base = 10) : s_(arduino_detail::integer(v, base)) {}
  /// @brief The digits of `v`.
  /// @param v The number.
  /// @param base Its base (see arduino_detail::integer()).
  explicit String(unsigned long long v, unsigned char base = 10)
      : s_(arduino_detail::integer(v, base)) {}
  /// @brief `v` with `decimals` decimals, padded on the left to at least
  /// `decimals` + 2 characters, as Arduino-ESP32 does: String(5.0f, 0) is
  /// " 5".
  /// @param v The number.
  /// @param decimals How many decimals.
  explicit String(float v, unsigned int decimals = 2)
      : s_(arduino_detail::dtostrf(v, static_cast<int>(decimals) + 2, decimals)) {}
  /// @brief `v` with `decimals` decimals, padded as String(float, unsigned int)
  /// is.
  /// @param v The number.
  /// @param decimals How many decimals.
  explicit String(double v, unsigned int decimals = 2)
      : s_(arduino_detail::dtostrf(v, static_cast<int>(decimals) + 2, decimals)) {}

  /// The text, null-terminated, until the String changes.
  const char* c_str() const { return s_.c_str(); }
  /// The length, in bytes.
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  /// Whether it's "".
  bool isEmpty() const { return s_.empty(); }
  /// @brief The character at `i`; `'\0'` past the end.
  /// @param i Its index, from 0.
  char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
  /// @brief The character at `i`; `'\0'` past the end.
  /// @param i Its index, from 0.
  char charAt(unsigned int i) const { return (*this)[i]; }

  /// @brief Where `c` first appears, from `from` on.
  /// @param c The character.
  /// @param from Where to start looking.
  /// @return Its index, or -1 if it isn't there.
  int indexOf(char c, unsigned int from = 0) const { return found(s_.find(c, from)); }
  /// @brief Where `text` first appears, from `from` on.
  /// @param text The text.
  /// @param from Where to start looking.
  /// @return Its index, or -1 if it isn't there.
  int indexOf(const char* text, unsigned int from = 0) const { return found(s_.find(text, from)); }
  /// @brief Where `c` last appears.
  /// @param c The character.
  /// @return Its index, or -1 if it isn't there.
  int lastIndexOf(char c) const { return found(s_.rfind(c)); }
  /// @brief Whether it starts with `text`.
  /// @param text The text.
  bool startsWith(const char* text) const { return s_.rfind(text, 0) == 0; }

  /// @brief The text from `from` on.
  /// @param from Where it starts.
  String substring(unsigned int from) const { return substring(from, length()); }
  /// @brief The text from `from` up to, not including, `to`.
  ///
  /// As Arduino's: the two are swapped if `from` is after `to`, and it's ""
  /// if `from` is past the end.
  /// @param from Where it starts.
  /// @param to Where it ends; at most the length.
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = length();
    return String(s_.substr(from, to - from));
  }
  /// @brief Removes everything from `index` on.
  /// @param index Where to start; past the end removes nothing.
  void remove(unsigned int index) {
    if (index < s_.size()) s_.erase(index);
  }
  /// @brief Removes `count` characters, or as many as there are.
  /// @param index Where to start; past the end removes nothing.
  /// @param count How many.
  void remove(unsigned int index, unsigned int count) {
    if (index < s_.size()) s_.erase(index, count);
  }

  /// @brief Whether the two hold the same text.
  /// @param other The other String.
  bool operator==(const String& other) const { return s_ == other.s_; }
  /// @brief Whether it holds `other`.
  /// @param other The text; nullptr counts as "".
  bool operator==(const char* other) const { return s_ == (other ? other : ""); }
  /// @brief Whether the two hold different text.
  /// @param other The other String.
  bool operator!=(const String& other) const { return !(*this == other); }
  /// @brief Whether it doesn't hold `other`.
  /// @param other The text; nullptr counts as "".
  bool operator!=(const char* other) const { return !(*this == other); }

  /// @brief Appends `rhs`.
  /// @param rhs The text.
  /// @return This String.
  String& operator+=(const String& rhs) {
    s_ += rhs.s_;
    return *this;
  }
  /// @brief Appends `rhs`.
  /// @param rhs The text; nullptr appends nothing.
  /// @return This String.
  String& operator+=(const char* rhs) {
    if (rhs) s_ += rhs;
    return *this;
  }
  /// @brief Appends a character.
  /// @param c The character.
  /// @return This String.
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }
  /// @brief Appends a number's digits, as String(v) writes them: a float
  /// with 2 decimals.
  /// @tparam T The number's type (see arduino_detail::kIsNumber).
  /// @param v The number.
  /// @return This String.
  template <typename T, typename = std::enable_if_t<arduino_detail::kIsNumber<T>>>
  String& operator+=(T v) {
    return *this += String(v);
  }

 private:
  /// @brief Turns std::string's index into Arduino's.
  /// @param pos Where something was found, or std::string::npos.
  /// @return The index, or -1 for npos.
  static int found(std::string::size_type pos) {
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
  }

  std::string s_;  ///< The text.
};

/// @brief `lhs` followed by `rhs`.
/// @param lhs The first part.
/// @param rhs The second.
inline String operator+(String lhs, const String& rhs) { return lhs += rhs; }
/// @brief `lhs` followed by `rhs`.
/// @param lhs The first part.
/// @param rhs The second; nullptr adds nothing.
inline String operator+(String lhs, const char* rhs) { return lhs += rhs; }
/// @brief `lhs` followed by `rhs`.
/// @param lhs The first part.
/// @param rhs The second.
inline String operator+(const char* lhs, const String& rhs) { return String(lhs) += rhs; }
/// @brief `lhs` followed by the character `c`.
/// @param lhs The text.
/// @param c The character.
inline String operator+(String lhs, char c) { return lhs += c; }
/// @brief `lhs` followed by the digits of `v` (see String::operator+=()).
/// @tparam T The number's type (see arduino_detail::kIsNumber).
/// @param lhs The text.
/// @param v The number.
template <typename T, typename = std::enable_if_t<arduino_detail::kIsNumber<T>>>
String operator+(String lhs, T v) {
  return lhs += v;
}
