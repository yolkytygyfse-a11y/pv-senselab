#pragma once
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace pv {
inline uint8_t rtdConfiguration(uint8_t physicalWires, bool compensation) {
  return 0x81 | ((physicalWires == 3 && compensation) ? 0x10 : 0);
}
enum class FrameResult { Ok, BadLength, WrongAddress, BadCrc, Exception,
                         BadFunction, EncodingUnknown, OutOfRange };
inline uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (unsigned j = 0; j < 8; ++j)
      crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
  }
  return crc;
}
inline uint16_t be16(const uint8_t* p) {
  return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}
inline FrameResult parseModbusRegisters(const uint8_t* p, size_t n, uint8_t address,
                                       uint8_t function, uint8_t count, uint16_t* out) {
  if (!out || count < 1 || count > 8 || n < 5) return FrameResult::BadLength;
  if (n != 5 && n != size_t(5 + 2 * count)) return FrameResult::BadLength;
  if (p[0] != address) return FrameResult::WrongAddress;
  if (crc16(p, n - 2) != (static_cast<uint16_t>(p[n-1]) << 8 | p[n-2]))
    return FrameResult::BadCrc;
  if (p[1] == (function | 0x80) && n == 5) return FrameResult::Exception;
  if ((function != 3 && function != 4) || p[1] != function) return FrameResult::BadFunction;
  if (n != size_t(5 + 2 * count) || p[2] != 2 * count) return FrameResult::BadLength;
  for (uint8_t i = 0; i < count; ++i) out[i] = be16(p + 3 + 2 * i);
  return FrameResult::Ok;
}
inline FrameResult parseModbus(const uint8_t* p, size_t n, uint8_t address, uint16_t& out) {
  return parseModbusRegisters(p, n, address, 3, 1, &out);
}
inline void makeEnvCommissionQuery(uint8_t address, bool writeTwo, uint8_t* out) {
  out[0] = address; out[1] = writeTwo ? 6 : 3;
  out[2] = 0; out[3] = 0x66; out[4] = 0; out[5] = writeTwo ? 2 : 1;
  uint16_t crc = crc16(out, 6); out[6] = crc & 255; out[7] = crc >> 8;
}
inline bool addressReadbackMatches(const uint8_t* p, size_t n, uint8_t address) {
  uint16_t value = 0;
  return parseModbus(p, n, address, value) == FrameResult::Ok && value == address;
}
inline bool rs485AddressesCompatible(bool irradianceEnabled, uint8_t irradianceAddress,
                                    bool envEnabled, uint8_t envAddress) {
  return !irradianceEnabled || !envEnabled || irradianceAddress != envAddress;
}
inline double pt100Resistance(double t) {
  constexpr double A = 3.9083e-3, B = -5.775e-7, C = -4.183e-12;
  return 100.0 * (1.0 + A*t + B*t*t + (t < 0 ? C*(t-100)*t*t*t : 0));
}
inline bool pt100Temperature(double resistance, double& out) {
  if (!std::isfinite(resistance) || resistance < pt100Resistance(-200) ||
      resistance > pt100Resistance(850)) return false;
  double lo = -200, hi = 850;
  for (unsigned i = 0; i < 48; ++i) {
    double mid = (lo + hi) * 0.5;
    if (pt100Resistance(mid) < resistance) lo = mid; else hi = mid;
  }
  out = (lo + hi) * 0.5;
  return true;
}
inline bool shuntCurrent(int16_t raw, double shuntOhm, double& out) {
  if (!std::isfinite(shuntOhm) || shuntOhm <= 0 || raw == INT16_MAX || raw == INT16_MIN)
    return false;
  out = raw * 2.5e-6 / shuntOhm;
  return std::isfinite(out);
}
inline FrameResult scaledIrradiance(uint16_t raw, double scale, double& out) {
  if (!std::isfinite(scale) || scale <= 0) return FrameResult::EncodingUnknown;
  const double value = raw * scale;
  if (!std::isfinite(value) || value > 2000) return FrameResult::OutOfRange;
  out = value; return FrameResult::Ok;
}
inline FrameResult scaledEnvironment(uint16_t raw, double scale, bool temperature,
                                      bool signedTemperatureConfirmed, double& out) {
  if (!std::isfinite(scale) || scale <= 0) return FrameResult::EncodingUnknown;
  if (temperature && raw >= 0x8000 && !signedTemperatureConfirmed)
    return FrameResult::EncodingUnknown;
  const int32_t code = temperature && raw >= 0x8000 ? int32_t(raw) - 65536 : int32_t(raw);
  const double value = code * scale;
  if (!std::isfinite(value) || (temperature ? (value < -30 || value > 85) : (value < 0 || value > 100)))
    return FrameResult::OutOfRange;
  out = value; return FrameResult::Ok;
}
inline bool parseUnsigned(const char* s, uint64_t& out) {
  if (!s || !*s) return false;
  uint64_t v = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    unsigned digit = static_cast<unsigned>(*s - '0');
    if (v > (UINT64_MAX - digit) / 10) return false;
    v = v * 10 + digit;
  }
  out = v; return true;
}
inline bool parseFinite(const char* s, double& out) {
  if (!s || !*s || *s == ' ' || *s == '\t') return false;
  errno = 0; char* end = nullptr;
  const double v = std::strtod(s, &end);
  if (errno || end == s || *end || !std::isfinite(v)) return false;
  out = v; return true;
}
inline const char* frameStatus(FrameResult r) {
  switch (r) {
    case FrameResult::Ok: return "ok";
    case FrameResult::BadLength: return "bad_length";
    case FrameResult::WrongAddress: return "wrong_address";
    case FrameResult::BadCrc: return "bad_crc";
    case FrameResult::Exception: return "modbus_exception";
    case FrameResult::BadFunction: return "bad_function";
    case FrameResult::EncodingUnknown: return "encoding_unconfirmed";
    case FrameResult::OutOfRange: return "out_of_range";
  }
  return "invalid_frame";
}
}  // namespace pv
