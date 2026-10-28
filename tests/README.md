# 验证计划与证据

状态：已有确定性 C++ 协议/数学测试与 Python 归档完整性测试；Arduino ESP32-S3 目标已实际编译。没有硬件通过记录。第一阶段结构检查与当前软件测试分别记录，均不能替代电气/通信/精度验证。

## 当前软件验证

从仓库根目录执行 `python3 -m unittest discover -s tests/unit -p 'test_*.py'`，验证准确字节保存、坏行/非有限值/重复键/超长行、分片/部分尾行、SHA-256 与禁止覆盖旧目录。C++17 编译并运行 tests/unit/test_sensor_math.cpp，验证手册 CRC/异常帧、PT100 参考点、正负分流、Modbus FC03/04 多字帧/长度和共线地址冲突及严格参数解析。

实际 macOS C++ 命令：`/Library/Developer/CommandLineTools/usr/bin/clang++ -std=c++17 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -Wall -Wextra -Werror tests/unit/test_sensor_math.cpp -o /tmp/pv-sensor-tests`，再运行 `/tmp/pv-sensor-tests`。Arduino 构建使用 `python3 firmware/build_arduino.py`。本机 Intel ctags 的局部 ARM64 适配说明在被忽略的 firmware/pv-sensing/build/tools，最初仅局部覆盖；后续 IDE 编译修复已备份并替换用户 Arduino15 中的 Intel ctags，系统工具未改。

2026-10-25 RS485 更正：2 项 Python 测试通过，C++ 协议/数学测试通过；Arduino-ESP32 3.3.11/ESP32S3 Dev Module/原生 USB CDC 构建退出 0，无编译诊断，Flash 356998 字节、全局 RAM 40840 字节。共线轮询/地址冲突/回显/缓冲/配置/初始化清理经独立源码复核，未发现实质问题；实物通信、负温编码、背压故障注入与标定未执行。

| 层级 | 范围 | 未来证据 |
|---|---|---|
| [unit](unit/README.md) | 单位/协议/数据校验、特征与物理公式边界 | 确切输入来源、预期推导、运行命令与输出 |
| [integration](integration/README.md) | 真实串口→落盘→重放→分析，ID/版本/失败流 | 设备/固件/标定、原始记录、摘要与复算输出 |
| [hardware](hardware/README.md) | 量程、SOA、热、接地、保护、校准和清洁互锁 | 仪表/配置、接线、故障注入与实际结果 |

重大变更必须列验证步骤、实际结果、未测边界。优先测试物理/协议不变量和真实失败模式，不写仅镜像实现的测试。无硬件时明确未运行，禁止假 API/模拟设备通过日志冒充实测。

验收门槛在相关 TBD 关闭后事前冻结，详见 [PRD](../docs/PRD.md) 与 [ROADMAP](../docs/ROADMAP.md)。原始证据按数据规则保留，不把 `tests/` 变为不可追溯附件堆。

## 第一阶段结构校验（2026-10-20）

已用 Python 标准库检查必需文件/目录、UTF-8 文本、本地 Markdown 链接/锚点、TBD 登记引用、11 条需求和 7 周条目；用 `git check-ignore --stdin` 检查原始数据/构建忽略与 README/标定/EDA 源保留；`git status --short --branch` 确认新仓库状态。结果记录在根 todo.md，不代表功能测试通过。

复核时重新枚举文件并解析相对链接；对 `.gitignore` 应同时检查应忽略和应保留路径，不能只确认命令退出成功。本机 `/usr/bin/git` 被 Xcode 许可检查拦截，本次使用可用的 `/Library/Developer/CommandLineTools/usr/bin/git`；没有修改系统许可或工具设置。

## SM7820B 更新的验证（2026-10-25）

新增确定性测试覆盖未设倍率、高位温度未确认、标准补码0xF617=-2537、0xffff=-1、温湿度越界，以及手册正确查询CRC与错误表格响应拒绝。手册正文响应 `01 03 04 00 7A 00 00 DB EA` CRC正确；表格 `01 03 04 00 79 00 7A DB EA` 应为AA09，程序严格拒绝后者。以上为文档/数学检查，无设备读数。

本次结果：C++测试通过，Python2项通过；Arduino-ESP32 3.3.11构建退出0，无编译诊断，Flash358478字节、全局RAM40856字节。独立源码审查无实质问题；交付副本五文件字节一致，硬件验证未运行。

2026-10-25 IDE失败排查：两份用户日志均在ctags阶段报bad CPU type in executable；uname为arm64，原工具file为x86_64，原工具执行失败，备份替换后的file为arm64且--version成功。修复后无ctags路径覆盖的真实CLI编译退出0，Flash354038字节、全局RAM40664字节，无项目编译诊断。未执行烧录或设备通信，不能称上传成功。

0.3.1真实故障修复验证：旧固件首笔配置读回失败为修复前证据；修复GPIO初始化顺序后，重启首笔及后4笔配置均通过。C++/Python测试和目标编译通过，Flash354190字节、全局RAM40664字节，实际上传Hash verified。证据在integration说明及data/raw/bench；Rref未知故温度null，RS485/INA未测试，保留1条启动非JSON。
