#pragma once
#include <cstddef>
#include <cstdint>

namespace cfg {
constexpr char VERSION[] = "0.6.0";
constexpr int SDA = 8, SCL = 9;
constexpr int RTD_CS = 10, MOSI = 11, SCK = 12, MISO = 13;
// Purchased module labels follow host UART: TXD input, RXD output (seller diagram).
constexpr int RS_DIR = 16, RS_TX = 18, RS_RX = 17;
constexpr uint32_t CONSOLE_BAUD = 115200, RS_BAUD = 9600;
constexpr uint32_t IO_TIMEOUT_MS = 25, RS_TIMEOUT_MS = 250, RS_QUIET_MS = 100;
constexpr uint32_t INA_TIMEOUT_MS = 25, RTD_CONVERSION_MS = 80;
constexpr size_t LINE_BYTES = 2048, OUTPUT_LINES = 8, COMMAND_BYTES = 192;
constexpr uint16_t INA_TRIGGER_CONFIG = 0x4123;  // AVG=1, bus/shunt=1.1ms, triggered.
enum Device : uint8_t { INA, RTD, RS485, ENV, COUNT };
struct Settings {
  bool enabled[COUNT] = {};  // Explicit opt-in after wiring/electrical checks.
  uint8_t inaAddress = 64;
  uint32_t periodMs = 1000;
  double shuntOhm = 0;       // Unknown, NOT an assumed module shunt value.
  double rrefOhm = 0;        // Unknown; check the actual MAX31865 board.
  bool rtdCompensation = true;  // Explicit opt-out is a labelled reference mode.
  uint8_t rtdWires = 0;      // Must explicitly choose 2, 3 or 4.
  double irradianceScale = 0;  // Unknown; manual's /100 is only hypothetical.
  int8_t rsMode = -1;        // -1 unknown, 0 automatic, 1 DE+/RE manual.
  uint8_t irradianceAddress = 1;  // SM9569 manual default, verify actual device.
  // SM7820B V24.3.19 defaults, not a readback of the actual device configuration.
  bool envProtocolConfirmed = true, envRegisterSet = true, envSm7820b = true;
  uint8_t envAddress = 2, envFunction = 3, envCount = 2;  // Register 0x66 read back as 2 on 2026-10-25.
  uint16_t envRegister = 0;
  double envScale = 0;       // p.8 calls /100 hypothetical; require explicit scale.
  bool envSignedTemperature = false;  // p.8's complement example is inconsistent.
};
}  // namespace cfg
