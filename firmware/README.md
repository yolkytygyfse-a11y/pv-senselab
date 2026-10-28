# 固件

状态：2026-10-25 已有可构建的 [Arduino 台架采集程序](pv-sensing/siri/README.md)，采用 Arduino-ESP32 3.3.11 自带的 Wire/SPI/UART/WiFi/WebServer/LittleFS，无额外 Arduino 库。用户要求的原型以此实现为主，正式自研板是否转 ESP-IDF 另评审，不维护重复驱动。P1 是逻辑规划，实物电平和安全验收尚未完成。

- [pv-sensing](pv-sensing/README.md)：ESP32-S3 应用、采集/扫描和状态机。
- [components](components/README.md)：随实际实现需要组织的驱动/协议职责，当前仅说明。

读取 [架构](../docs/ARCHITECTURE.md)、[协议](../docs/API_PROTOCOL.md)、[传感器](../docs/SENSOR_SPEC.md) 和两板接口后修改；更新 `todo.md`。电子负载当前无独立 MCU，不为其生成空固件。安全关断不能仅靠主机或固件；任何控制改动都检查量程、SOA、热、接地、保护与校准。

真实构建入口：`python3 firmware/build_arduino.py`；它只编译、不上传。源码、参数、控制台和逐通道步骤见 Arduino 说明；协议/数学及归档有主机测试，已有RS485/参考PT100与0.6.0实机HTTP短测记录；故障注入和完整精度/长时验收尚未完成。不要将编译通过当作接线/保护/标定通过。
