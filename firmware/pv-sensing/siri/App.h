#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <cstdarg>
#include <cstring>
#include "Config.h"
#include "SensorMath.h"
#include "WebDashboard.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This firmware requires an ESP32-S3 target."
#endif

namespace app {
cfg::Settings settings;
HardwareSerial rs(1);
bool streaming = false, wireReady = false, spiReady = false;
bool outputFault = false, clockKnown = false;
bool webInfoPending = false;
uint64_t seq = 0, dropped = 0, clockUncertainty = 0;
uint64_t clockMappedAt = 0;
int64_t clockOffset = 0;
uint32_t revision = 0;
dashboard::Web web(settings, streaming, revision);
char deviceId[17], bootId[17];
uint64_t due[cfg::COUNT] = {};
constexpr uint64_t us(uint32_t ms) { return static_cast<uint64_t>(ms) * 1000; }
uint64_t now() { return static_cast<uint64_t>(esp_timer_get_time()); }

// Fixed-size output queue: never wait indefinitely for a USB terminal.
char output[cfg::OUTPUT_LINES][cfg::LINE_BYTES];
size_t lengths[cfg::OUTPUT_LINES] = {}, qHead = 0, qTail = 0, qCount = 0, qOffset = 0;
struct Line {
  char text[cfg::LINE_BYTES] = {};
  size_t length = 0;
  bool valid = true;
  void add(const char* format, ...) {
    if (!valid) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(text + length, sizeof(text) - length, format, args);
    va_end(args);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(text) - length) valid = false;
    else length += static_cast<size_t>(n);
  }
  void number(double v) { if (std::isfinite(v)) add("%.9g", v); else add("null"); }
  void hex(const uint8_t* p, size_t n) {
    add("\""); for (size_t i = 0; i < n; ++i) add("%02X", p[i]); add("\"");
  }
};
void prefix(Line& l, const char* type) {
  const uint64_t t = now();
  l.add("{\"profile\":\"pv-senselab-arduino/2\",\"type\":\"%s\","
        "\"firmware\":\"%s\",\"device_id\":\"%s\",\"boot_id\":\"%s\","
        "\"seq\":%llu,\"t_mono_us\":%llu,\"utc_estimate_us\":", type, cfg::VERSION,
        deviceId, bootId, static_cast<unsigned long long>(seq++), static_cast<unsigned long long>(t));
  if (clockKnown) l.add("%lld", static_cast<long long>(static_cast<int64_t>(t) + clockOffset));
  else l.add("null");
  // One host command is only an approximate mapping, not a drift-calibrated clock.
  l.add(",\"utc_uncertainty_us\":null,\"host_declared_uncertainty_us\":");
  if (clockKnown) l.add("%llu", static_cast<unsigned long long>(clockUncertainty)); else l.add("null");
  l.add(",\"clock_mapping_age_us\":");
  if (clockKnown) l.add("%llu", static_cast<unsigned long long>(t-clockMappedAt)); else l.add("null");
  l.add(",\"config_revision\":%lu,\"dropped_records\":%llu,\"run_id\":null,"
        "\"calibration_id\":null", static_cast<unsigned long>(revision), static_cast<unsigned long long>(dropped));
}
void emit(Line& l) {
  l.add("}\n");
  if (l.valid) web.append(l.text, l.length);
  if (!l.valid || qCount == cfg::OUTPUT_LINES) { ++dropped; outputFault = true; return; }
  memcpy(output[qTail], l.text, l.length);
  lengths[qTail] = l.length; qTail = (qTail + 1) % cfg::OUTPUT_LINES; ++qCount;
}
void event(const char* name, const char* status) {
  Line l; prefix(l, "event"); l.add(",\"event\":\"%s\",\"status\":\"%s\"", name, status); emit(l);
}
void drainOutput() {
  if (!qCount) return;
  int capacity = Serial.availableForWrite();
  if (capacity <= 0) return;
  size_t n = lengths[qHead] - qOffset;
  if (n > static_cast<size_t>(capacity)) n = capacity;
  if (n > 128) n = 128;
  qOffset += Serial.write(reinterpret_cast<const uint8_t*>(output[qHead]) + qOffset, n);
  if (qOffset == lengths[qHead]) { qOffset = 0; qHead = (qHead + 1) % cfg::OUTPUT_LINES; --qCount; }
}
void samplePrefix(Line& l, const char* sensor, uint64_t start, bool uncompensated = false) {
  prefix(l, "measurement");
  l.add(",\"sensor\":\"%s\",\"sample_start_us\":%llu,\"sample_end_us\":%llu,"
        "\"quality_flags\":[\"UNCALIBRATED\",\"TIME_ESTIMATED\"%s%s]", sensor,
        static_cast<unsigned long long>(start), static_cast<unsigned long long>(now()),
        clockKnown ? "" : ",\"TIME_UNSYNCED\"",
        uncompensated ? ",\"LEAD_RESISTANCE_UNCOMPENSATED\"" : "");
}
void errorSample(const char* sensor, const char* status, uint64_t start) {
  if (!strcmp(sensor,"max31865")) web.observe(3,now(),NAN,NAN,status);
  Line l; samplePrefix(l, sensor, start, !strcmp(sensor,"max31865") && settings.rtdWires == 3 && !settings.rtdCompensation);
  l.add(",\"status\":\"%s\",\"value\":null", status); emit(l);
}

bool readIna(uint8_t reg, uint16_t& v) {
  Wire.beginTransmission(settings.inaAddress); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(settings.inaAddress, size_t(2), true) != 2) return false;
  v = static_cast<uint16_t>(Wire.read()) << 8; v |= Wire.read(); return true;
}
bool writeIna(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(settings.inaAddress); Wire.write(reg);
  Wire.write(static_cast<uint8_t>(v >> 8)); Wire.write(static_cast<uint8_t>(v));
  return Wire.endTransmission(true) == 0;
}
bool inaPending = false;
uint64_t inaStart = 0;
void serviceIna(uint64_t t) {
  if (!settings.enabled[cfg::INA]) return;
  if (!inaPending) {
    if (t < due[cfg::INA]) return;
    due[cfg::INA] = t + us(settings.periodMs);
    uint16_t maker = 0, die = 0;
    if (!wireReady || !readIna(0xfe, maker) || !readIna(0xff, die)) {
      errorSample("ina226", "i2c_error", t); return;
    }
    if (maker != 0x5449 || (die & 0xfff0) != 0x2260) {
      errorSample("ina226", "device_identity_mismatch", t); return;
    }
    inaStart = now();
    if (!writeIna(0, cfg::INA_TRIGGER_CONFIG)) { errorSample("ina226", "i2c_error", t); return; }
    inaPending = true; return;
  }
  uint16_t mask = 0;
  if (!readIna(6, mask)) { inaPending = false; errorSample("ina226", "i2c_error", inaStart); return; }
  if (!(mask & 8)) {
    if (now() - inaStart > us(cfg::INA_TIMEOUT_MS)) {
      inaPending = false; errorSample("ina226", "conversion_timeout", inaStart);
    }
    return;
  }
  inaPending = false;
  uint16_t rawV = 0, rawShunt = 0, conf = 0;
  if (!readIna(2, rawV) || !readIna(1, rawShunt) || !readIna(0, conf)) {
    errorSample("ina226", "i2c_error", inaStart); return;
  }
  int32_t signedShunt = rawShunt >= 0x8000 ? static_cast<int32_t>(rawShunt) - 65536 : rawShunt;
  double current = NAN;
  const bool validV = rawV * 0.00125 <= 36.0;
  bool validI = pv::shuntCurrent(static_cast<int16_t>(signedShunt), settings.shuntOhm, current);
  Line l; samplePrefix(l, "ina226", inaStart);
  const bool rail = signedShunt == INT16_MIN || signedShunt == INT16_MAX;
  l.add(",\"status\":\"%s\",\"raw_v_code\":%u,\"raw_i_code\":%ld,"
        "\"configuration_register\":%u,\"mask_register\":%u,\"shunt_ohm\":",
        (!validV || rail) ? "out_of_range" : (validI ? "ok_uncalibrated" : "shunt_unconfigured"),
        rawV, static_cast<long>(signedShunt), conf, mask);
  if (settings.shuntOhm > 0) l.number(settings.shuntOhm); else l.add("null");
  l.add(",\"bus_voltage_v\":"); l.number(validV ? rawV * 0.00125 : NAN);
  l.add(",\"shunt_voltage_v\":"); l.number(rail ? NAN : signedShunt * 2.5e-6);
  l.add(",\"current_a\":"); l.number(validI ? current : NAN);
  l.add(",\"power_w\":"); l.number(validV && validI ? rawV * 0.00125 * current : NAN);
  l.add(",\"vi_skew_us\":null"); emit(l);
}

uint8_t rtdBase() { return pv::rtdConfiguration(settings.rtdWires, settings.rtdCompensation); }
uint8_t rtdRead(uint8_t reg) {
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
  digitalWrite(cfg::RTD_CS, LOW); SPI.transfer(reg & 0x7f);
  uint8_t v = SPI.transfer(0xff); digitalWrite(cfg::RTD_CS, HIGH); SPI.endTransaction(); return v;
}
void rtdWrite(uint8_t reg, uint8_t v) {
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
  digitalWrite(cfg::RTD_CS, LOW); SPI.transfer(reg | 0x80); SPI.transfer(v);
  digitalWrite(cfg::RTD_CS, HIGH); SPI.endTransaction();
}
uint8_t rtdPhase = 0;
uint64_t rtdStart = 0, rtdWait = 0;
void serviceRtd(uint64_t t) {
  if (!settings.enabled[cfg::RTD]) return;
  const bool referenceMode = settings.rtdWires == 3 && !settings.rtdCompensation;
  if (!rtdPhase) {
    if (t < due[cfg::RTD]) return;
    due[cfg::RTD] = t + us(settings.periodMs); rtdStart = t;
    if (!spiReady) { errorSample("max31865", "spi_init_failed", t); return; }
    rtdWrite(0, rtdBase() | 2);
    uint8_t observed = rtdRead(0);
    if ((observed & 0xd1) != rtdBase()) {
      web.observe(3,now(),NAN,NAN,"configuration_readback_failed");
      rtdWrite(0, (rtdBase() & ~0x80) | 2);
      Line l; samplePrefix(l, "max31865", t, referenceMode);
      l.add(",\"status\":\"configuration_readback_failed\",\"value\":null,"
            "\"expected_configuration\":%u,\"observed_configuration\":%u", rtdBase(), observed);
      emit(l); return;
    }
    rtdWait = t + us(12); rtdPhase = 1; return;
  }
  if (t < rtdWait) return;
  if (rtdPhase == 1) {
    rtdWrite(0, rtdBase() | 0x20); rtdWait = now() + us(cfg::RTD_CONVERSION_MS); rtdPhase = 2; return;
  }
  rtdPhase = 0;
  SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE1));
  digitalWrite(cfg::RTD_CS, LOW); SPI.transfer(1);
  uint16_t raw = static_cast<uint16_t>(SPI.transfer(0xff)) << 8; raw |= SPI.transfer(0xff);
  digitalWrite(cfg::RTD_CS, HIGH); SPI.endTransaction();
  uint8_t fault = rtdRead(7), config = rtdRead(0);
  rtdWrite(0, (rtdBase() & ~0x80) | 2);  // Bias off between conversions: reduce self-heating.
  uint16_t code = raw >> 1; double resistance = NAN, temperature = NAN;
  bool good = !(raw & 1) && fault == 0 && code > 0 && code < 32767 && !(config & 0x20);
  if (good && settings.rrefOhm > 0) {
    resistance = code * settings.rrefOhm / 32768.0;
    good = pv::pt100Temperature(resistance, temperature);
  }
  const char* rtdStatus = !good ? "sensor_fault" : (settings.rrefOhm <= 0 ? "rref_unconfigured" : referenceMode ? "reference_uncompensated" : "ok_uncalibrated");
  web.observe(3,now(),good ? temperature : NAN,code,rtdStatus);
  Line l; samplePrefix(l, "max31865", rtdStart, referenceMode);
  l.add(",\"status\":\"%s\",\"raw_rtd_register\":%u,\"rtd_code\":%u,"
        "\"fault_register\":%u,\"configuration_register\":%u,\"rtd_wires\":%u,\"rref_ohm\":",
        !good ? "sensor_fault" : (settings.rrefOhm <= 0 ? "rref_unconfigured" : referenceMode ? "reference_uncompensated" : "ok_uncalibrated"),
        raw, code, fault, config, settings.rtdWires);
  l.number(settings.rrefOhm > 0 ? settings.rrefOhm : NAN);
  l.add(",\"rtd_compensation_enabled\":%s", (config & 0x10) ? "true" : "false");
  l.add(",\"resistance_ohm\":"); l.number(good ? resistance : NAN);
  l.add(",\"temperature_c\":"); l.number(good ? temperature : NAN); emit(l);
}

uint8_t rsBuffer[32] = {}, rsQuery[8] = {};
size_t rsLength = 0;
bool rsPending = false, rsOverflow = false;
uint64_t rsStart = 0, rsLastByte = 0, rsQuietUntil = 0;
cfg::Device rsOwner = cfg::RS485, rsNext = cfg::RS485;
bool rsEnabled() { return settings.enabled[cfg::RS485] || settings.enabled[cfg::ENV]; }
void finishRs(const char* overrideStatus = nullptr) {
  rsPending = false; rsQuietUntil = now() + us(cfg::RS_QUIET_MS);
  size_t offset = rsLength >= 8 && memcmp(rsBuffer, rsQuery, 8) == 0 ? 8 : 0;
  uint16_t words[8] = {};
  const uint8_t count = rsQuery[5];
  auto parsed = pv::parseModbusRegisters(rsBuffer + offset, rsLength - offset,
                                        rsQuery[0], rsQuery[1], count, words);
  const bool ok = !overrideStatus && parsed == pv::FrameResult::Ok;
  const char* status = overrideStatus ? overrideStatus : pv::frameStatus(parsed);
  double irradiance = NAN;
  auto scaled = pv::scaledIrradiance(words[0], settings.irradianceScale, irradiance);
  if (ok) {
    if (rsOwner == cfg::ENV) status = "frame_ok";
    else if (scaled == pv::FrameResult::EncodingUnknown) status = "scale_unconfirmed";
    else if (scaled == pv::FrameResult::OutOfRange) status = "out_of_range";
  }
  Line l; samplePrefix(l, rsOwner == cfg::ENV ? "environment_rs485" : "sm9569", rsStart);
  l.add(",\"status\":\"%s\",\"request_hex\":", status); l.hex(rsQuery, 8);
  l.add(",\"response_hex\":"); l.hex(rsBuffer, rsLength);
  l.add(",\"request_echo_bytes\":%u,\"slave_address\":%u,\"function_code\":%u,"
        "\"register_address\":%u,\"register_count\":%u,\"raw_registers\":",
        static_cast<unsigned>(offset), rsQuery[0], rsQuery[1], pv::be16(rsQuery + 2), count);
  if (ok) {
    l.add("[");
    for (uint8_t i = 0; i < count; ++i) l.add("%s%u", i ? "," : "", words[i]);
    l.add("]");
  } else l.add("null");
  if (rsOwner == cfg::ENV) {
    double temperature = NAN, humidity = NAN;
    const bool decode = ok && settings.envSm7820b && rsQuery[1] == 3 &&
                        pv::be16(rsQuery + 2) == 0 && count == 2;
    auto tr = decode ? pv::scaledEnvironment(words[0], settings.envScale, true,
                                            settings.envSignedTemperature, temperature)
                     : pv::FrameResult::EncodingUnknown;
    auto hr = decode ? pv::scaledEnvironment(words[1], settings.envScale, false, false, humidity)
                     : pv::FrameResult::EncodingUnknown;
    const char* ts = !ok ? status : !decode ? "raw_only" : settings.envScale <= 0 ? "scale_unconfirmed"
                         : tr == pv::FrameResult::Ok ? "ok_uncalibrated" : pv::frameStatus(tr);
    const char* hs = !ok ? status : !decode ? "raw_only" : settings.envScale <= 0 ? "scale_unconfirmed"
                         : hr == pv::FrameResult::Ok ? "ok_uncalibrated" : pv::frameStatus(hr);
    web.observe(1,now(),decode && settings.envScale<=0 ? words[0] : tr==pv::FrameResult::Ok ? temperature : NAN,ok?words[0]:NAN,ts);
    web.observe(2,now(),decode && settings.envScale<=0 ? words[1] : hr==pv::FrameResult::Ok ? humidity : NAN,ok?words[1]:NAN,hs);
    l.add(",\"env_profile\":\"%s\",\"env_scale\":", settings.envSm7820b ? "sm7820b" : "raw");
    l.number(settings.envScale > 0 ? settings.envScale : NAN);
    l.add(",\"env_signed_temp_confirmed\":%s,\"temperature_status\":\"%s\","
          "\"humidity_status\":\"%s\",\"temperature_c\":",
          settings.envSignedTemperature ? "true" : "false", ts, hs);
    l.number(tr == pv::FrameResult::Ok ? temperature : NAN);
    l.add(",\"relative_humidity_pct\":"); l.number(hr == pv::FrameResult::Ok ? humidity : NAN);
  } else {
    web.observe(0,now(),ok && settings.irradianceScale<=0 ? words[0] : ok && scaled==pv::FrameResult::Ok ? irradiance : NAN,ok?words[0]:NAN,status);
    l.add(",\"raw_register\":"); if (ok) l.add("%u", words[0]); else l.add("null");
    l.add(",\"irradiance_scale\":"); l.number(settings.irradianceScale > 0 ? settings.irradianceScale : NAN);
    l.add(",\"irradiance_w_m2\":"); l.number(ok && scaled == pv::FrameResult::Ok ? irradiance : NAN);
    l.add(",\"scale_status\":\"%s\"", settings.irradianceScale > 0 ? "user_configured_not_calibrated" : "SCALE_UNCONFIRMED");
  }
  if (parsed == pv::FrameResult::Exception && rsLength - offset == 5)
    l.add(",\"exception_code\":%u", rsBuffer[offset + 2]);
  emit(l);
}
void serviceRs(uint64_t t) {
  if (!rsEnabled()) return;
  if (!rsPending) {
    // Keep unexpected/late bytes observable. Require a quiet gap before transmitting.
    uint8_t stale[32]; size_t n = 0;
    while (rs.available() && n < sizeof(stale)) stale[n++] = rs.read();
    if (n) {
      Line l; prefix(l, "event"); l.add(",\"event\":\"rs485_unsolicited\",\"raw_hex\":"); l.hex(stale,n); emit(l);
      rsQuietUntil = now() + us(cfg::RS_QUIET_MS); return;
    }
    if (t < rsQuietUntil) return;
    cfg::Device other = rsNext == cfg::RS485 ? cfg::ENV : cfg::RS485;
    if (settings.enabled[rsNext] && t >= due[rsNext]) rsOwner = rsNext;
    else if (settings.enabled[other] && t >= due[other]) rsOwner = other;
    else return;
    rsNext = rsOwner == cfg::RS485 ? cfg::ENV : cfg::RS485;
    due[rsOwner] = t + us(settings.periodMs);
    const bool env = rsOwner == cfg::ENV;
    const uint16_t reg = env ? settings.envRegister : 0;
    rsQuery[0] = env ? settings.envAddress : settings.irradianceAddress;
    rsQuery[1] = env ? settings.envFunction : 3;
    rsQuery[2] = reg >> 8; rsQuery[3] = reg & 255;
    rsQuery[4] = 0; rsQuery[5] = env ? settings.envCount : 1;
    uint16_t crc = pv::crc16(rsQuery, 6); rsQuery[6] = crc & 255; rsQuery[7] = crc >> 8;
    rsLength = 0; rsOverflow = false; rsStart = now(); rsLastByte = rsStart;
    if (settings.rsMode == 1) digitalWrite(cfg::RS_DIR, HIGH);
    size_t sent = rs.write(rsQuery, 8); rs.flush(true);
    if (settings.rsMode == 1) digitalWrite(cfg::RS_DIR, LOW);
    rsPending = true;
    if (sent != 8) finishRs("uart_write_failed");
    return;
  }
  unsigned budget = 64;
  while (rs.available() && budget--) {
    int b = rs.read(); rsLastByte = now();
    if (rsLength < sizeof(rsBuffer)) rsBuffer[rsLength++] = static_cast<uint8_t>(b);
    else rsOverflow = true;
  }
  if (rsOverflow) { finishRs("frame_overflow"); return; }
  t = now();
  size_t offset = rsLength >= 8 && memcmp(rsBuffer, rsQuery, 8) == 0 ? 8 : 0;
  if (rsLength > offset && t - rsLastByte >= us(5)) { finishRs(); return; }
  if (t - rsStart >= us(cfg::RS_TIMEOUT_MS)) finishRs("timeout");
}

void stop() {
  if (rsPending) finishRs("cancelled");
  if (inaPending) errorSample("ina226", "cancelled", inaStart);
  if (rtdPhase) errorSample("max31865", "cancelled", rtdStart);
  if (spiReady) { rtdWrite(0, 0x01); SPI.end(); spiReady = false; }
  if (wireReady) { writeIna(0, 0x4120); Wire.end(); wireReady = false; }
  if (rsEnabled()) { rs.end(); if (settings.rsMode == 1) digitalWrite(cfg::RS_DIR, LOW); }
  inaPending = false; rtdPhase = 0; rsPending = false;
  streaming = false;
}
bool start() {
  bool any = false; for (bool enabled : settings.enabled) any |= enabled;
  if (!any) { event("start", "no_devices_enabled"); return false; }
  if (settings.enabled[cfg::RTD] && settings.rtdWires == 0) { event("start", "set_rtd_wires_first"); return false; }
  if (rsEnabled() && settings.rsMode < 0) { event("start", "set_rs485_mode_first"); return false; }
  if (settings.enabled[cfg::ENV] && (!settings.envProtocolConfirmed || !settings.envAddress ||
      !settings.envRegisterSet || !settings.envCount || !settings.envFunction)) {
    event("start", "environment_modbus_profile_required"); return false;
  }
  if (settings.enabled[cfg::ENV] && uint32_t(settings.envRegister) + settings.envCount > 65536) {
    event("start", "environment_register_range_invalid"); return false;
  }
  if (settings.enabled[cfg::ENV] && settings.envSm7820b &&
      (settings.envFunction != 3 || settings.envRegister != 0 || settings.envCount != 2)) {
    event("start", "sm7820b_register_map_mismatch"); return false;
  }
  if (!pv::rs485AddressesCompatible(settings.enabled[cfg::RS485], settings.irradianceAddress,
                                    settings.enabled[cfg::ENV], settings.envAddress)) {
    event("start", "rs485_address_conflict"); return false;
  }
  if (settings.enabled[cfg::INA]) {
    wireReady = Wire.begin(cfg::SDA, cfg::SCL, 100000); Wire.setTimeOut(cfg::IO_TIMEOUT_MS);
    if (!wireReady) { Wire.end(); stop(); event("start", "i2c_init_failed"); return false; }
  }
  if (settings.enabled[cfg::RTD]) {
    pinMode(cfg::RTD_CS, OUTPUT); digitalWrite(cfg::RTD_CS, HIGH);
    spiReady = SPI.begin(cfg::SCK, cfg::MISO, cfg::MOSI, cfg::RTD_CS);
    if (!spiReady) { SPI.end(); stop(); event("start", "spi_init_failed"); return false; }
  }
  if (rsEnabled()) {
    if (settings.rsMode == 1) { pinMode(cfg::RS_DIR, OUTPUT); digitalWrite(cfg::RS_DIR, LOW); }
    rs.begin(cfg::RS_BAUD, SERIAL_8N1, cfg::RS_RX, cfg::RS_TX);
    if (!rs) { stop(); event("start", "uart_init_failed"); return false; }
  }
  rsNext = cfg::RS485; rsQuietUntil = now() + us(cfg::RS_QUIET_MS);
  for (auto& deadline : due) deadline = now();
  streaming = true; event("stream", "started"); return true;
}
void status() {
  Line l; prefix(l, "status");
  l.add(",\"streaming\":%s,\"period_ms\":%lu,\"ina_enabled\":%s,\"rtd_enabled\":%s,"
        "\"rs485_enabled\":%s,\"env_enabled\":%s,\"ina_address\":%u,\"shunt_ohm\":",
        streaming ? "true":"false", static_cast<unsigned long>(settings.periodMs),
        settings.enabled[0]?"true":"false",settings.enabled[1]?"true":"false",
        settings.enabled[2]?"true":"false",settings.enabled[3]?"true":"false", settings.inaAddress);
  l.number(settings.shuntOhm > 0 ? settings.shuntOhm : NAN);
  l.add(",\"rtd_compensation_enabled\":%s", settings.rtdWires == 3 && settings.rtdCompensation ? "true" : "false");
  l.add(",\"rtd_wires\":%u,\"rref_ohm\":",settings.rtdWires); l.number(settings.rrefOhm > 0 ? settings.rrefOhm : NAN);
  l.add(",\"rs485_mode\":%d,\"irradiance_scale\":", settings.rsMode); l.number(settings.irradianceScale > 0 ? settings.irradianceScale : NAN);
  l.add(",\"rs485_baud\":%lu,\"rs485_format\":\"8N1\",\"irradiance_address\":%u,"
        "\"env_protocol_confirmed\":%s,\"env_address\":%u,\"env_function\":%u,\"env_register\":",
        static_cast<unsigned long>(cfg::RS_BAUD), settings.irradianceAddress,
        settings.envProtocolConfirmed ? "true" : "false", settings.envAddress, settings.envFunction);
  if (settings.envRegisterSet) l.add("%u", settings.envRegister); else l.add("null");
  l.add(",\"env_count\":%u,\"env_profile\":\"%s\",\"env_scale\":", settings.envCount,
        settings.envSm7820b ? "sm7820b" : "raw");
  l.number(settings.envScale > 0 ? settings.envScale : NAN);
  l.add(",\"env_signed_temp_confirmed\":%s",settings.envSignedTemperature ? "true" : "false"); emit(l);
}
int deviceIndex(const char* name) {
  const char* names[] = {"ina", "rtd", "rs485", "env"};
  for (unsigned i = 0; i < cfg::COUNT; ++i) if (!strcmp(name,names[i])) return i;
  return -1;
}
bool setValue(const char* key, const char* value) {
  double d = 0; uint64_t u = 0;
  if (!strcmp(key,"rs485_mode")) {
    if (!strcmp(value,"auto")) settings.rsMode=0;
    else if (!strcmp(value,"manual")) settings.rsMode=1; else return false;
  } else if (!strcmp(key,"env_protocol")) {
    if (!strcmp(value,"modbus")) settings.envProtocolConfirmed=true;
    else if (!strcmp(value,"none")) settings.envProtocolConfirmed=false; else return false;
  } else if (!strcmp(key,"env_profile")) {
    if (!strcmp(value,"sm7820b")) settings.envSm7820b=true;
    else if (!strcmp(value,"raw")) settings.envSm7820b=false; else return false;
  } else if (!strcmp(key,"env_scale")) {
    if (!pv::parseFinite(value,d) || d<0 || d>100) return false; settings.envScale=d;
  } else if (!strcmp(key,"env_signed_temp")) {
    if (!pv::parseUnsigned(value,u) || u>1) return false; settings.envSignedTemperature=u;
  } else if (!strcmp(key,"rtd_compensation")) {
    if (!pv::parseUnsigned(value,u) || u>1) return false; settings.rtdCompensation = u != 0;
  } else if (!strcmp(key,"rtd_wires")) {
    if (!pv::parseUnsigned(value,u) || (u!=2 && u!=3 && u!=4)) return false;
    settings.rtdWires=u;
  } else if (!strcmp(key,"ina_address")) {
    if (!pv::parseUnsigned(value,u) || u<64 || u>79) return false; settings.inaAddress=u;
  } else if (!strcmp(key,"period_ms")) {
    if (!pv::parseUnsigned(value,u) || u<100 || u>60000) return false; settings.periodMs=u;
  } else if (!strcmp(key,"irradiance_address") || !strcmp(key,"env_address")) {
    if (!pv::parseUnsigned(value,u) || u<1 || u>247) return false;
    if (!strcmp(key,"irradiance_address")) settings.irradianceAddress=u; else settings.envAddress=u;
  } else if (!strcmp(key,"env_function")) {
    if (!pv::parseUnsigned(value,u) || (u!=3 && u!=4)) return false; settings.envFunction=u;
  } else if (!strcmp(key,"env_register")) {
    if (!pv::parseUnsigned(value,u) || u>65535) return false;
    settings.envRegister=u; settings.envRegisterSet=true;
  } else if (!strcmp(key,"env_count")) {
    if (!pv::parseUnsigned(value,u) || u<1 || u>8) return false; settings.envCount=u;
  } else if (!strcmp(key,"shunt_ohm") || !strcmp(key,"rref_ohm") || !strcmp(key,"irradiance_scale")) {
    if (!pv::parseFinite(value,d) || d<=0) return false;
    if (!strcmp(key,"shunt_ohm")) { if(d>100) return false; settings.shuntOhm=d; }
    else if (!strcmp(key,"rref_ohm")) { if(d<350 || d>10000) return false; settings.rrefOhm=d; }
    else { if(d>2000) return false; settings.irradianceScale=d; }
  } else return false;
  ++revision; return true;
}
// Bounded commissioning in STOPPED state; no arbitrary register-write interface.
struct AddressResponse {
  uint8_t data[32] = {};
  size_t size = 0, echo = 0;
  bool sent = false, overflow = false, unsolicited = false;
};
AddressResponse addressExchange(const uint8_t* query, const char* phase) {
  AddressResponse response;
  // Conservative bench gap: 100ms ordinary polling worked; minimum is uncharacterized.
  delay(cfg::RS_QUIET_MS);
  if (rs.available()) {
    response.unsolicited = true;
    while (rs.available() && response.size < sizeof(response.data)) response.data[response.size++] = rs.read();
  } else {
    if (settings.rsMode == 1) digitalWrite(cfg::RS_DIR, HIGH);
    response.sent = rs.write(query, 8) == 8; rs.flush(true);
    if (settings.rsMode == 1) digitalWrite(cfg::RS_DIR, LOW);
    uint64_t started = now(), lastByte = started;
    while (response.sent && now() - started < us(cfg::RS_TIMEOUT_MS)) {
      unsigned budget = 64;
      while (rs.available() && budget--) {
        int b = rs.read(); lastByte = now();
        if (response.size < sizeof(response.data)) response.data[response.size++] = b;
        else response.overflow = true;
      }
      if (response.overflow) break;
      bool echoOnly = response.size == 8 && !memcmp(response.data, query, 8);
      if (response.size >= 5 && !echoOnly && now() - lastByte >= us(5)) break;
      drainOutput(); delay(1);
    }
  }
  // A single FC06 response may equal an adapter echo; never treat it as verification.
  response.echo = response.size > 8 && !memcmp(response.data, query, 8) ? 8 : 0;
  Line l; prefix(l, "event");
  l.add(",\"event\":\"env_address_setup\",\"phase\":\"%s\",\"request_hex\":", phase); l.hex(query, 8);
  l.add(",\"response_hex\":"); l.hex(response.data, response.size);
  l.add(",\"echo_bytes\":%u,\"transport_status\":\"%s\"", static_cast<unsigned>(response.echo),
        response.unsolicited ? "unsolicited_before_request" : !response.sent ? "uart_write_failed" :
        response.overflow ? "frame_overflow" : !response.size ? "timeout" : "received_unclassified"); emit(l);
  return response;
}
bool addressVerified(const AddressResponse& r, uint8_t address) {
  return r.sent && !r.overflow && !r.unsolicited &&
         pv::addressReadbackMatches(r.data + r.echo, r.size - r.echo, address);
}
void commissionEnvAddress() {
  for (bool enabled : settings.enabled) {
    if (enabled) { event("env_address_change", "disable_all_channels_first"); return; }
  }
  if (settings.rsMode < 0 || !settings.envSm7820b || !settings.envProtocolConfirmed) {
    event("env_address_change", "rs485_mode_and_sm7820b_profile_required"); return;
  }
  if (qCount > 1) { event("env_address_change", "output_busy_retry_after_drain"); return; }
  if (settings.rsMode == 1) { pinMode(cfg::RS_DIR, OUTPUT); digitalWrite(cfg::RS_DIR, LOW); }
  rs.begin(cfg::RS_BAUD, SERIAL_8N1, cfg::RS_RX, cfg::RS_TX);
  if (!rs) { rs.end(); event("env_address_change", "uart_init_failed"); return; }
  uint8_t query[8];
  pv::makeEnvCommissionQuery(1, false, query);
  auto before = addressExchange(query, "read_old");
  for (unsigned attempt = 1; attempt < 3 && !addressVerified(before, 1) && !before.unsolicited; ++attempt) {
    before = addressExchange(query, "read_old");
  }
  if (!addressVerified(before, 1)) {
    rs.end(); event("env_address_change", "precheck_failed_no_write"); return;
  }
  pv::makeEnvCommissionQuery(1, true, query);
  auto written = addressExchange(query, "write_once");
  if (!written.sent || written.unsolicited) {
    rs.end(); event("env_address_change", written.sent ? "outcome_unknown" : "write_not_sent"); return;
  }
  // Verify at the NEW address even when the write acknowledgement was lost/echoed.
  pv::makeEnvCommissionQuery(2, false, query);
  auto after = addressExchange(query, "verify_new");
  for (unsigned attempt = 1; attempt < 3 && !addressVerified(after, 2) && !after.unsolicited; ++attempt) {
    after = addressExchange(query, "verify_new");
  }
  bool verified = addressVerified(after, 2);
  rs.end();
  if (verified) { settings.envAddress = 2; ++revision; }
  event("env_address_change", verified ? "verified_1_to_2" : "outcome_unknown_do_not_repeat_write");
}
void command(char* line) {
  char* tokens[5] = {}; unsigned n = 0; char* save = nullptr;
  for (char* p = strtok_r(line," \t\r",&save); p && n<5; p=strtok_r(nullptr," \t\r",&save)) tokens[n++]=p;
  if (!n) return;
  if (n==1 && !strcmp(tokens[0],"web")) { webInfoPending=true; return; }
  if (n==1 && !strcmp(tokens[0],"stop")) { stop(); event("stream","stopped"); return; }
  if (n==1 && !strcmp(tokens[0],"status")) { status(); return; }
  if (n==1 && !strcmp(tokens[0],"help")) {
    Line l; prefix(l,"help"); l.add(",\"commands\":[\"status\",\"start\",\"stop\","
      "\"web\",\"enable|disable ina|rtd|rs485|env\",\"set key value\",\"time unix_us uncertainty_us\",\"commission_env_1_to_2 single_device\"],"
      "\"set_keys\":[\"ina_address\",\"period_ms\",\"shunt_ohm\",\"rref_ohm\",\"rtd_wires\",\"rtd_compensation\","
      "\"rs485_mode\",\"irradiance_scale\",\"irradiance_address\",\"env_protocol\","
      "\"env_address\",\"env_function\",\"env_register\",\"env_count\",\"env_profile\",\"env_scale\",\"env_signed_temp\"]"); emit(l); return;
  }
  if (n==3 && !strcmp(tokens[0],"time")) {
    uint64_t epoch=0, uncertainty=0;
    if (!pv::parseUnsigned(tokens[1],epoch) || epoch<946684800000000ULL || epoch>4102444800000000ULL ||
        !pv::parseUnsigned(tokens[2],uncertainty) || uncertainty==0 || uncertainty>86400000000ULL) {
      event("command","invalid_time"); return;
    }
    clockMappedAt=now();
    clockOffset=static_cast<int64_t>(epoch)-static_cast<int64_t>(clockMappedAt);
    clockUncertainty=uncertainty; clockKnown=true; event("clock","host_time_mapping_set"); return;
  }
  if (streaming) { event("command","stop_before_configuration"); return; }
  if (n==2 && !strcmp(tokens[0],"commission_env_1_to_2") && !strcmp(tokens[1],"single_device")) {
    commissionEnvAddress(); return;
  }
  if (n==1 && !strcmp(tokens[0],"start")) { web.store.reset(); start(); return; }
  if (n==2 && (!strcmp(tokens[0],"enable") || !strcmp(tokens[0],"disable"))) {
    int i=deviceIndex(tokens[1]);
    if (i<0) { event("command","unknown_device"); return; }
    settings.enabled[i]=!strcmp(tokens[0],"enable"); ++revision; event("command","configured"); return;
  }
  if (n==3 && !strcmp(tokens[0],"set") && setValue(tokens[1],tokens[2])) {
    web.store.reset();
    event("command","configured"); return;
  }
  event("command","invalid_command_or_value");
}
char commandBuffer[cfg::COMMAND_BYTES]; size_t commandLength=0; bool badCommand=false;
void readCommands() {
  for (unsigned budget=0; budget<64 && Serial.available() && qCount+2<cfg::OUTPUT_LINES; ++budget) {
    int c=Serial.read();
    if (c=='\n') {
      if (badCommand) event("command","invalid_or_overlong_line");
      else { commandBuffer[commandLength]=0; command(commandBuffer); }
      commandLength=0; badCommand=false;
    } else if (c==0 || (c<32 && c!='\r' && c!='\t') || c>126 || commandLength+1>=sizeof(commandBuffer)) {
      badCommand=true;
    } else if (!badCommand) commandBuffer[commandLength++]=static_cast<char>(c);
  }
}
bool startWebSampling() {
  // Current bench profile, explicitly started by an authenticated local operator.
  // Do not connect INA226 or enable any power/cleaning output.
  settings.enabled[cfg::INA]=false;
  settings.enabled[cfg::RTD]=settings.enabled[cfg::RS485]=settings.enabled[cfg::ENV]=true;
  settings.rtdWires=3; settings.rtdCompensation=false;
  if (settings.rrefOhm<=0) settings.rrefOhm=428; // User's in-circuit meter report; NOT calibration.
  settings.rsMode=0; settings.irradianceAddress=1; settings.envAddress=2;
  settings.envProtocolConfirmed=settings.envRegisterSet=settings.envSm7820b=true;
  settings.envFunction=3; settings.envRegister=0; settings.envCount=2;
  ++revision; web.store.reset();
  status(); return start();
}
void setup() {
  Serial.begin(cfg::CONSOLE_BAUD);  // Never wait for a USB host to connect.
  snprintf(deviceId,sizeof(deviceId),"%016llx",static_cast<unsigned long long>(ESP.getEfuseMac()));
  snprintf(bootId,sizeof(bootId),"%08lx%08lx",static_cast<unsigned long>(esp_random()),static_cast<unsigned long>(esp_random()));
  web.begin(deviceId,bootId,startWebSampling,stop);
  event("boot","idle_all_devices_disabled");
  status();
}
void tick() {
  drainOutput(); readCommands();
  if (webInfoPending && !qCount) { web.printConnection(); webInfoPending=false; }
  web.tick();
  if (web.fault() && streaming) { stop(); event("archive","storage_fault_sampling_stopped"); }
  if (outputFault) {
    if (streaming) stop();
    if (qCount+2<cfg::OUTPUT_LINES) { outputFault=false; event("output","overflow_sampling_stopped"); }
  }
  if (streaming && qCount+4<cfg::OUTPUT_LINES) {
    serviceIna(now()); serviceRtd(now()); serviceRs(now());
  } else if (streaming && (inaPending || rtdPhase || rsPending)) {
    // In-flight acquisitions cannot silently time out while waiting for the host.
    stop(); event("output","backpressure_sampling_stopped");
  }
  delay(1);  // Yield to USB, UART and FreeRTOS; no long conversion delay in the loop.
}
}  // namespace app
