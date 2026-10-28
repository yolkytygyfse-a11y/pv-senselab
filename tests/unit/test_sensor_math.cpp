// Deterministic protocol/reference fixtures; these are NOT experiment data.
#include "../../firmware/pv-sensing/siri/SensorMath.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
  using namespace pv;
  // ADI config register: 3 physical leads and compensation are independent.
  // Explicit reference mode MUST clear D4 while retaining physical wire count.
  assert(rtdConfiguration(3, true) == 0x91);
  assert(rtdConfiguration(3, false) == 0x81);
  assert(rtdConfiguration(2, true) == 0x81);
  assert(rtdConfiguration(4, true) == 0x81);
  const uint8_t query[] = {1, 3, 0, 0, 0, 1};
  assert(crc16(query, sizeof(query)) == 0x0a84);
  const uint8_t reply[] = {1, 3, 2, 2, 0x18, 0xb9, 0x2e};
  uint16_t value = 0;
  assert(parseModbus(reply, sizeof(reply), 1, value) == FrameResult::Ok);
  assert(value == 536);  // Manual raw register, not an irradiance measurement.
  assert(parseModbus(reply, 6, 1, value) == FrameResult::BadLength);
  assert(parseModbus(reply, 7, 2, value) == FrameResult::WrongAddress);
  uint8_t corrupt[7];
  for (size_t i = 0; i < 7; ++i) corrupt[i] = reply[i];
  corrupt[3] ^= 1;
  assert(parseModbus(corrupt, 7, 1, value) == FrameResult::BadCrc);
  uint8_t exception[] = {1, 0x83, 2, 0, 0};
  const uint16_t c = crc16(exception, 3);
  exception[3] = c & 255; exception[4] = c >> 8;
  assert(parseModbus(exception, 5, 1, value) == FrameResult::Exception);

  // Commissioning must use only the address register and verify the returned value.
  uint8_t addressQuery[8] = {};
  makeEnvCommissionQuery(1, true, addressQuery);
  const uint8_t writeAddressTwo[] = {1, 6, 0, 0x66, 0, 2, 0xe8, 0x14};
  for (size_t i = 0; i < 8; ++i) assert(addressQuery[i] == writeAddressTwo[i]);
  // 15-readonly-register-and-cadence confirmed the address register returns 1.
  // Require the register value AND frame address, not ordinary data as a proxy.
  const uint8_t readOne[] = {1, 3, 0, 0x66, 0, 1, 0x64, 0x15};
  const uint8_t readTwo[] = {2, 3, 0, 0x66, 0, 1, 0x64, 0x26};
  makeEnvCommissionQuery(1, false, addressQuery);
  for (size_t i = 0; i < 8; ++i) assert(addressQuery[i] == readOne[i]);
  makeEnvCommissionQuery(2, false, addressQuery);
  for (size_t i = 0; i < 8; ++i) assert(addressQuery[i] == readTwo[i]);
  const uint8_t addressOne[] = {1, 3, 2, 0, 1, 0x79, 0x84};
  const uint8_t addressTwo[] = {2, 3, 2, 0, 2, 0x7d, 0x85};
  const uint8_t wrongValue[] = {2, 3, 2, 0, 1, 0x3d, 0x84};
  assert(addressReadbackMatches(addressOne, 7, 1));
  assert(addressReadbackMatches(addressTwo, 7, 2));
  assert(!addressReadbackMatches(addressOne, 7, 2));
  assert(!addressReadbackMatches(wrongValue, 7, 2));
  assert(!addressReadbackMatches(addressOne, 6, 1));
  assert(!addressReadbackMatches(exception, 5, 1));
  const uint8_t envOne[] = {1, 3, 4, 0x0a, 0x42, 0x17, 0x2c, 0x57, 0xd2};
  assert(!addressReadbackMatches(envOne, 9, 1));  // Measurement is not address readback.

  double t = 0;
  assert(pt100Temperature(100.0, t) && std::fabs(t) < 0.0001);
  assert(pt100Temperature(138.5055, t) && std::fabs(t - 100) < 0.0001);
  assert(pt100Temperature(80.306281875, t) && std::fabs(t + 50) < 0.0001);
  assert(!pt100Temperature(0, t));
  assert(!pt100Temperature(std::numeric_limits<double>::quiet_NaN(), t));

  double a = 0;
  assert(shuntCurrent(4000, 0.01, a) && std::fabs(a - 1) < 1e-9);
  assert(shuntCurrent(-4000, 0.01, a) && std::fabs(a + 1) < 1e-9);
  assert(!shuntCurrent(4000, 0, a));
  assert(!shuntCurrent(32767, 0.01, a));  // Saturated rail must not look valid.

  // Analytic Modbus framing fixture; NOT this unconfirmed sensor's register map.
  uint8_t block[] = {2, 4, 4, 0x12, 0x34, 0xfe, 0xdc, 0, 0};
  const uint16_t blockCrc = crc16(block, 7);
  block[7] = blockCrc & 255; block[8] = blockCrc >> 8;
  uint16_t words[8] = {};
  assert(parseModbusRegisters(block, 9, 2, 4, 2, words) == FrameResult::Ok);
  assert(words[0] == 0x1234 && words[1] == 0xfedc);
  assert(parseModbusRegisters(block, 9, 2, 3, 2, words) == FrameResult::BadFunction);
  assert(parseModbusRegisters(block, 9, 2, 4, 1, words) == FrameResult::BadLength);
  assert(parseModbusRegisters(block, 9, 2, 4, 9, words) == FrameResult::BadLength);
  assert(!rs485AddressesCompatible(true, 1, true, 1));
  assert(rs485AddressesCompatible(true, 1, true, 2));
  assert(rs485AddressesCompatible(true, 1, false, 0));
  assert(scaledIrradiance(3000, 1, a) == FrameResult::OutOfRange);
  assert(scaledIrradiance(1000, 0.5, a) == FrameResult::Ok && a == 500);
  assert(scaledIrradiance(1000, 0, a) == FrameResult::EncodingUnknown);

  // Catch guessed scale, unsigned negative temperature and one-count sign errors.
  // Mathematical fixtures, not readings from the user's instrument.
  assert(scaledEnvironment(2500, 0.01, true, false, a) == FrameResult::Ok && a == 25);
  assert(scaledEnvironment(6000, 0.01, false, false, a) == FrameResult::Ok && a == 60);
  assert(scaledEnvironment(2500, 0, true, false, a) == FrameResult::EncodingUnknown);
  assert(scaledEnvironment(0xf617, 0.01, true, false, a) == FrameResult::EncodingUnknown);
  assert(scaledEnvironment(0xf617, 0.01, true, true, a) == FrameResult::Ok);
  assert(std::fabs(a + 25.37) < 1e-9);  // 62999 - 65536 = -2537, not -2536.
  assert(scaledEnvironment(0xffff, 0.01, true, true, a) == FrameResult::Ok);
  assert(std::fabs(a + 0.01) < 1e-9);
  assert(scaledEnvironment(0x8000, 0.01, true, true, a) == FrameResult::OutOfRange);
  assert(scaledEnvironment(8501, 0.01, true, true, a) == FrameResult::OutOfRange);
  assert(scaledEnvironment(10001, 0.01, false, true, a) == FrameResult::OutOfRange);

  const uint8_t envQuery[] = {1, 3, 0, 0, 0, 2};
  assert(crc16(envQuery, 6) == 0x0bc4);  // SM7820B p.7 query.
  const uint8_t envReply[] = {1, 3, 4, 0, 0x7a, 0, 0, 0xdb, 0xea};
  assert(parseModbusRegisters(envReply, 9, 1, 3, 2, words) == FrameResult::Ok);
  assert(words[0] == 122 && words[1] == 0);  // Manual example counts only.
  const uint8_t inconsistentReply[] = {1, 3, 4, 0, 0x79, 0, 0x7a, 0xdb, 0xea};
  assert(parseModbusRegisters(inconsistentReply, 9, 1, 3, 2, words) == FrameResult::BadCrc);

  uint64_t u = 0;
  assert(parseUnsigned("18446744073709551615", u) && u == UINT64_MAX);
  assert(!parseUnsigned("18446744073709551616", u));
  assert(!parseUnsigned("-1", u) && !parseUnsigned("1tail", u));
  assert(!parseUnsigned("", u) && !parseUnsigned("+1", u));
  assert(parseFinite("0.01", a) && std::fabs(a - 0.01) < 1e-12);
  assert(!parseFinite("nan", a) && !parseFinite("inf", a));
  assert(!parseFinite("1x", a) && !parseFinite("1e999", a));
  std::puts("PASS: CRC, multi-register frames, shared-bus conflicts, SM7820B scale/sign/ranges, RTD, shunt and strict configuration parsing");
}
