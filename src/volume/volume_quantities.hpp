#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>

class FixedGain16 {
public:
  explicit constexpr FixedGain16(int32_t value) : value_(value) {}
  static constexpr FixedGain16 unity() { return FixedGain16{65536}; }
  constexpr int32_t value() const { return value_; }
  auto operator<=>(const FixedGain16 &) const = default;
  friend void PrintTo(FixedGain16 gain, std::ostream *out) {
    *out << gain.value_ << " (Q16 gain)";
  }
private:
  int32_t value_;
};

class CentibelAttenuation {
public:
  constexpr CentibelAttenuation() : value_(0) {}
  explicit constexpr CentibelAttenuation(double value) : value_(value) {}
  constexpr double value() const { return value_; }
  CentibelAttenuation wholeCentibels() const {
    return CentibelAttenuation{static_cast<double>(static_cast<int32_t>(std::trunc(value_)))};
  }
  FixedGain16 fixedGain() const {
    return FixedGain16{static_cast<int32_t>(65536.0 * std::pow(10, value_ / 2000))};
  }
  auto operator<=>(const CentibelAttenuation &) const = default;
  friend constexpr CentibelAttenuation operator+(CentibelAttenuation left, CentibelAttenuation right) {
    return CentibelAttenuation{left.value_ + right.value_};
  }
  friend constexpr CentibelAttenuation operator-(CentibelAttenuation left, CentibelAttenuation right) {
    return CentibelAttenuation{left.value_ - right.value_};
  }
  friend void PrintTo(CentibelAttenuation attenuation, std::ostream *out) {
    *out << attenuation.value_ << " cB";
  }
private:
  double value_;
};

class Decibels {
public:
  constexpr Decibels() : value_(0) {}
  explicit constexpr Decibels(double value) : value_(value) {}
  constexpr double value() const { return value_; }
  constexpr CentibelAttenuation inCentibels() const { return CentibelAttenuation{value_ * 100}; }
  CentibelAttenuation maximumAttenuation() const {
    return CentibelAttenuation{static_cast<double>(static_cast<int32_t>(value_) * 100)};
  }
  auto operator<=>(const Decibels &) const = default;
  friend void PrintTo(Decibels level, std::ostream *out) { *out << level.value_ << " dB"; }
private:
  double value_;
};

class AirPlayVolume {
public:
  explicit constexpr AirPlayVolume(double value) : value_(value) {}
  static AirPlayVolume fromWireParameter(std::string_view text) {
    const std::string parameter(text);
    return AirPlayVolume{static_cast<float>(std::atof(parameter.c_str()))};
  }
  constexpr double value() const { return value_; }
  constexpr bool isMute() const { return value_ == -144; }
  bool isPlayable() const { return value_ >= -30 && value_ <= 0 && std::isfinite(value_); }
  auto operator<=>(const AirPlayVolume &) const = default;
  friend void PrintTo(AirPlayVolume level, std::ostream *out) { *out << level.value_ << " (AirPlay volume)"; }
private:
  double value_;
};
