# ESP32-S3 Arduino 采集程序

版本 0.6.0，2026-10-25。源码包括 siri.ino、App.h、Config.h、SensorMath.h、DashboardStore.h、WebDashboard.h 与生成的 DashboardPage.h，使用 Arduino-ESP32 **3.3.11** 自带 Wire/SPI/UART/WiFi/WebServer/LittleFS，无额外 Arduino 库。此目录为版本控制主源；用户 Arduino/siri 目录交付相同副本，后续修改须同步，不维护两套不同逻辑。

## 能力和边界

- INA226：检查厂家/器件 ID，触发转换、检查就绪/超时，保留电压/分流原码；已明确分流值时计算电流和功率。量程/饱和/通信失败不输出伪测量。
- PT100/MAX31865：明确线制后单次转换，偏置建立/转换等待由状态机推进；保留原码/故障/配置，明确 Rref 后采用 alpha=0.00385 的 PT100 Callendar–Van Dusen 关系换算，包含负温。间隔关闭偏置以减小自热；不宣称探头或整链已标定。
- SM9569：9600/8N1、地址 1、0x03/0x0000/数量 1 为手册候选；帧长度/功能/地址/CRC、异常帧、回显、溢出和超时处理。倍率未明确时只报原值，绝不套用手册“假设 /100”。地址可用 set irradiance_address 配置；当前总线固定 9600/8N1，实物必须匹配。
- SM7820B：已核对 V24.3.19 手册，RS485 Modbus RTU、9600/8N1、默认地址1，FC03读取PDU0温度/PDU1湿度。支持双字原码、CRC/异常/回显/超时、显式倍率与标准补码选项，各通道独立量程/状态。env_scale 未设不输出工程量；高位温度默认不解释，避免照抄手册减65535错误。所有通道仍上电关闭，不自动写仪表参数；地址1改2须显式维护命令，速率/校正值无写接口。
- USB/UART 控制台 JSONL 输出，设备/启动标识、序号、单调微秒、配置修订、空值原因、输出丢失计数。有限输出队列分片发送，终端背压时停止采样，需明确 start 重启；不静默覆盖。

这是**只读台架采集程序**，不实现 I-V 扫描、电子负载控制、清洁、ARM、安全闭环或完整 API_PROTOCOL 草案。无任何负载/执行器 GPIO。50W 组件的范围/量程、SOA、散热、接地、保护与校准未通过，不由本程序自动批准接入。0.6.0新增内部Flash只追加原始日志；空间有限，不能替代电脑独立备份。

## P1 引脚

| 接口 | GPIO |
|---|---|
| INA226 SDA / SCL | 8 / 9 |
| MAX31865 CS / MOSI(SDI) / SCK / MISO(SDO) | 10 / 11 / 12 / 13 |
| UART1 RS485 模块输入 / 输出 | TX18 / RX17；所购模块TXD→18、RXD→17 |
| RS485 DE+/RE 手动方向 | 16；自动方向时不连接 |

GPIO 是设计候选编号，不是排针位置。实际板引出、模块供电/3.3V 电平、PT100 线制/Rref 和 INA226 分流/额定须先核对。工业传感器用经过预算的独立电源。完整接线门槛在 hardware/pv-sensing-board/INTERFACES.md 的 P1，未经核对不通电。

## 编译

Arduino IDE 打开本目录 siri.ino，选择 ESP32S3 Dev Module；本次测试配置为 USB Mode=Hardware CDC and JTAG、USB CDC On Boot=Enabled、PSRAM disabled，其他 Flash/板型设置按实物。使用板上原生 USB 时开启 CDC；若改用 USB-UART 桥，按连接端口选择 CDC disabled，并用 --fqbn 明确编译选项。程序不使用 PSRAM，但不能据此猜测未知模组的 Flash 模式/容量。

仓库根目录执行 `python3 firmware/build_arduino.py`；可用 `--sketch /绝对路径/Arduino/siri` 编译 Arduino 副本。脚本输出到 firmware/pv-sensing/build/，不上传，不改安装目录，不自动安装依赖。已安装核心须为本次测试版本，升级后重新验证。

本机最初安装的 Arduino ctags 为 x86_64，在 ARM64 Mac 上报 bad CPU type in executable，发生于编译而非串口上传。最初仅构建脚本使用从 Arduino 官方 5.8-arduino11 源构建的原生工具；2026-10-25 为修复 IDE 默认编译，已将用户 Arduino15/packages/builtin/tools/ctags/5.8-arduino11/ctags 替换为同一 ARM64 工具，原件保存为 ctags.intel-backup-20261025，同目录保留源码包、许可证和修复摘要。没有安装 Rosetta、修改系统工具或更换固件逻辑。板卡管理器更新可能覆盖该本机修复。

修复后直接使用 Arduino CLI 默认工具路径（没有 --build-property ctags 覆盖，也未使用构建包装脚本），按最新日志的 esp32:esp32:esp32s3 默认选项编译用户 Arduino/siri 成功：Flash354038字节、全局RAM40664字节，退出0。此默认配置 CDC On Boot=Disabled；原生USB串口与USB-UART桥的配置要按实际插口选择。日志曾选XIAO，后已改选通用S3；实际板型/Flash/PSRAM仍须核对，不据编译通过确认接线或上电合格。尚未执行上传或硬件测试。

## 上电与控制台

115200 baud，ASCII 命令以换行 LF 结束，最长 191 字节；输出全部是 JSON 对象，每行一条。上电只输出启动/配置，**所有通道默认关闭**，且不等待 USB 连接。配置不写 NVS，复位后清空，避免误复用另一批模块的参数。

连接核对后，可先验证 INA226 原码/电压：

```text
enable ina
start
status
stop
```

分流未知时 current_a/power_w 为 null，status=shunt_unconfigured；这不是检测失败，也不代表电流为零。芯片电压为 VBUS 节点电压，模块接线决定其与 PV 端电压的关系，不能未经拓扑核对冒称组件端子电压。

| 命令 | 条件/作用 |
|---|---|
| help / status | 查看命令/实际配置，采样期间也可用 |
| enable 或 disable + ina/rtd/rs485/env | 停止状态配置通道，不立即驱动接口 |
| start / stop | 开始采集/中止在途采样并停用总线；初始化失败撤销启动 |
| set ina_address 数值 | 十进制 64–79；默认 64 对应 0x40，按实际 A0/A1 核对 |
| set period_ms 数值 | 100–60000；默认 1000，调度策略而非精度/吞吐承诺 |
| set shunt_ohm 数值 | 从模块/实测取得，单位 Ω；不提供未经核对的默认值 |
| set rtd_wires 数值 | 仅 2、3、4；必须匹配探头和板上跳线，未设时拒绝启动 RTD |
| set rref_ohm 数值 | 模块实际参考电阻，350–10000Ω；未设只报 RTD 原码 |
| set rs485_mode auto 或 manual | 必须核对方向电路后设置；未设拒绝启动 RS485 |
| set irradiance_scale 数值 | 厂家确认的 W/m² 每 count 系数；值=raw×scale；不是假定除数，不带未知偏移校正 |
| set irradiance_address 数值 | 已确认的辐照度从站地址，1–247，手册候选默认 1；只改变主站查询，不写仪表地址 |
| set env_protocol modbus 或 none | SM7820B 手册已确认 Modbus；默认 modbus，none 禁止启动环境通道 |
| set env_address 数值 | 实际温湿度从站地址，1–247，本项目目标默认2（厂家出厂1）；同总线上所有实物从站须唯一 |
| set env_function / env_register 数值 | 分别为 3 或 4，以及十进制 PDU 地址 0–65535；SM7820B 默认 FC03 / PDU0 |
| set env_count 数值 | 默认 2；SM7820B 模式必须 FC03/PDU0/数量2，raw 模式允许1–8且不越界 |
| set env_profile sm7820b 或 raw | 默认 sm7820b，限制已确认双寄存器映射；raw仅归档通用寄存器不换算 |
| set env_scale 数值 | 每count系数，0表示未知/撤销换算，范围0–100；确认实际倍率100时设置0.01，不将示例当校准 |
| set env_signed_temp 0 或 1 | 默认0，高位温度null；确认标准16位补码后1按raw−65536解释，仍受-30–85°C约束 |
| time unix_us uncertainty_us | 主机提供 2000–2100 年 UTC 近似映射与正的声明不确定度；不替代时钟校准 |

除 time/status/help/stop 外，采样期间拒绝配置。所有参数必须完整合法，拒绝 NaN/Infinity、溢出、尾随字符、未知命令及超长行；普通采集不写仪表；唯一例外是下文受限改址维护命令。修改设置后先 status，审查无误再 start。共用总线仅允许一条在途请求，逐台轮询，检测已启用通道地址冲突；异常/取消保留原帧。

PT100 先配置实际线制，再 enable rtd；Rref 未确认只保留原码。enable rs485 读取 SM9569；enable env 读取 SM7820B，默认候选映射已知，实际地址/速率另核对。RS485 方向模式仍必须按模块实际配置。

## 两只 RS485 仪表共用总线

UART1 是采集串口，固定 9600/8N1；控制台 Serial 为 115200，两者独立。现有一个 RS485 转 TTL 模块可供两只兼容仪表共用：主干 A 对各已确认 A，B 对各已确认 B，短支线；核对参考地/隔离、终端和偏置。TX18→所购模块TXD输入，模块RXD输出→RX17，手动 DE+/RE→GPIO16（自动方向不接）。模块 RO 必须适配 3.3V。GPIO4/5 现不分配，无需 CAN 模块。

共享前分别只连接一只仪表，以厂家工具/规程核实并设置不同地址与共同 9600/8N1；常规采集不代写仪表参数，维护例外见下节。只在程序里改地址不会改设备。即便关闭软件某通道，仍接在总线上的同址仪表也可能同时回复。SM7820B 与 SM9569 手册默认地址均为 1，因此合并前必须核实/调整；不能直接把两只出厂同址仪表并接。若实际协议/通信参数无法统一，改用独立总线并另行修订接线与固件。

## SM7820B 首次单独验证

仅连接这一只 RS485 仪表，供电/模块电平/参考地与方向已核对后，选择实际的 rs485_mode（auto或manual），再发送：

```text
enable env
status
start
stop
```

程序按手册候选地址1读FC03/PDU0/数量2；未改配置的查询为 `01 03 00 00 00 02 C4 0B`。若设备已改地址，先 set env_address 为实际值。第一次保留原码核对，不以 CRC 正确推定倍率/准确度正确。确认实际倍率100后，在停止状态 `set env_scale 0.01` 才得到温度/湿度工程值；不默认自动执行。确认标准补码后才 `set env_signed_temp 1`。手册负温例 raw=0xF617，标准补码换算为-25.37（scale0.01），与手册-25.36相差一个count，此处是数学核对，不是测量结果。

SM7820B 手册 p.4 图示：红电源+、绿电源−、黄A+、蓝B−；无原厂引线时线色仅供参考。手册供电DC5–24V，不能将其电源正极接ESP32信号脚。SM9569需要DC12–24V，共用12V仅为核对后的候选，不能据两份“1A”推定实际电流预算。

环境记录 status=frame_ok 只表示帧通过；温湿度须分别检查 temperature_status/humidity_status。倍率未知为scale_unconfirmed，高位温度未确认补码为encoding_unconfirmed，越界为out_of_range，正常换算为ok_uncalibrated。raw模式为raw_only，错误帧的两通道均无工程值。无效/未知均保留null，不能把frame_ok当测量合格。

## 数据解释与归档

输出 profile=`pv-senselab-arduino/2`，**不同于未来正式 protocol_version=1 的 JSON 命令接口**；这是明确的台架协议版本。测量 status 和附加字段给出失效/未配置原因，value/current/temperature 等无效值为 JSON null。quality_flags 始终保留 UNCALIBRATED 和 TIME_ESTIMATED，不伪造 calibration_id/run_id；正式实验前需建立完整配置与标定关联。

t_mono_us 是设备记录时刻；sample_start/end 是采样事务的近似窗口，不是同步 ADC 的精确转换窗。INA226 的 vi_skew_us 未证实时为 null。UTC 未设置为 null；time 命令后给出 utc_estimate_us、映射 age 和 host_declared_uncertainty_us；总 utc_uncertainty_us 仍为 null，因为漂移/传输延时未校准。不得以电脑收到时间冒充采样时间。

电脑端使用标准库工具：

```text
python3 software/capture_serial.py --port /dev/cu.实际端口 --output data/raw/bench/本次唯一目录 --commands /绝对路径/已审查命令.txt
```

不要同时用 Arduino 串口监视器和归档工具读取同一端口。命令文件每行一条，空行和 # 注释跳过；可先只放 enable ina、start、status。工具等待 1 秒后按 0.2 秒间隔发送，保存准确的 commands.bin；不会自动提供未知参数或伪时间。

保存 transport.bin 原始 RX 字节、records.jsonl（额外标注主机收时）与 SHA-256 manifest。新目录不得已存在；错误 JSON、启动噪声和部分末行仍在原始字节中。约每秒 fsync，Ctrl-C 正常关闭归档，端口故障标 failed；进程被强杀可能保留 in_progress，不能当完整记录。独立备份/恢复仍须另落实，本工具不代表备份已完成。

## 验证与已知限制

主机验证命令：`python3 -m unittest discover -s tests/unit -p 'test_*.py'`；C++ 关键逻辑测试为 tests/unit/test_sensor_math.cpp，可用 C++17 编译器运行，macOS 需显式选择可用 SDK。测试是确定性的协议/数学/文件完整性夹具，绝不是实验数据。

已完成115200状态查询及一次MAX31865原码诊断/初始化修复；尚无完整测温、RS485/INA通信、精度、时间同步、长期稳定性、保护或50W功率验证。正式集成前逐路验证：设备缺席/断线、寄存器读回、CRC/异常/超时、PT100 开短路、参考温度、RS485 双从站地址冲突/异常帧/负温编码、终端背压、停止/复位和归档恢复。USB 拔线/断电可能丢失 RAM 与尚未 fsync 数据；seq/dropped_records 与 manifest 用于暴露缺口，不承诺零丢失。

0.3.1修复：CS及手动RS485方向先pinMode后digitalWrite，适配Arduino-ESP32 3.3.11 GPIO管理规则；MAX31865配置失败保留期望/实读配置并尝试关闭偏置。INA226仍默认关闭，不使用伪值代替未连接设备。已重新启动实物验证：首笔及后4笔配置读回通过，故障寄存器0；Rref未知仍温度null，不能据此宣称测温合格。

## 温湿度地址1改2的受限维护

用户确认总线上物理上只有SM7820B且授权改址后，在停止且四个通道均disabled的状态，按模块真实方向设置rs485_mode，再发 `commission_env_1_to_2 single_device`。该确认不可由软件自动判断；两只同址共线时禁止使用。维护仅访问地址寄存器PDU0x0066：先读1号并核对CRC、应答地址及值均为1，再向1号发一次FC06写2，最后在2号读取并核对地址及值均为2。`verified_1_to_2`表示新地址寄存器已读回，不表示断电保存已验证；`precheck_failed_no_write`没有发写帧，`outcome_unknown_do_not_repeat_write`须分别只读核查1/2，不能重复写。

固定9600/8N1、每阶段250ms超时，维护最坏约3秒内完成，期间无其他采集/功率动作。每阶段保存原始请求/应答/回显与状态；不把写回显当成功，不写波特率或校正值。0.4.1主站env_address恢复为工厂候选1，改址读回通过后才更新为2；实际改址完成与否以设备读回记录为准，不以status里的配置变量为准。

## 所购四针模块接线更正（0.4.1）

用户商品说明明确TX-TX、RX-RX直连，并标注VCC为3.3V或5V。保留用户已确认的物理线：模块TXD→GPIO18、RXD→GPIO17、GND→主控GND、VCC→3V3；将UART1改为TX18/RX17，自动收发模式不使用GPIO16。半双工在单一A/B对上先发后收；与TTL侧引脚丝印采用哪一端视角是两个问题。此前TX17/RX18与这份商品说明不符。

商品照片可见MAX485/CD4069，同外观板存在相反引脚定义的逆向资料；实物芯片/板版本仍需核对，以本次商品页为接线诊断依据，不推广到其他模块。MAX485厂家规定5V±5%，商品3.3V标称与芯片保证范围冲突；保留当前3.3V进行有记录的联调，不据此宣称电气合格，也不把VCC盲改5V直连ESP32。供电/双向TTL电平、参考地、保护/热和波形验收仍属TBD-04、10。此次固件只更正UART方向和尚未改址的查询默认值，不驱动PV/负载/清洁。

0.4.2修正维护查询：SM7820B p.8指定FA 03 00 66 00 01 71 9E查询设备地址，回复首字节为实际地址。实测0.4.1普通数据帧两次CRC正确，但以01查询地址寄存器仍无回复且未写入；维护读阶段改用FA，仍检查返回地址和寄存器值分别等于目标1/2，仍仅允许单仪表，写帧最多一次。该通用地址不是Modbus广播0，不允许多仪表连接时使用。新增手册字节夹具先失败，再修正生成逻辑。

0.4.3依据实物差异修正：10-env-fa-commission的FA地址查询仍无应答且未写入；不再以无响应的元数据寄存器作唯一预检。使用已读通的FC03/PDU0/数量2，前检应答地址1、CRC/长度/功能/数量正确，写后同样要求地址2应答。只确认操作地址改变，不把普通数据内容当设备型号证明，不宣称地址寄存器读回或非易失保存已验证。继续要求用户确认总线仅一只SM7820B，写帧仍只有一次。

0.4.4：12-env-control-probe在同一0.4.3固件普通采集路径出现首笔timeout、第二笔frame_ok。因此前述地址寄存器超时不证明寄存器不受支持，UART/自动方向模块启动暂态及3.3V供电影响仍未分离。维护前后普通数据只读校验各最多3次（每次250ms），遇前置无请求数据立即停止；写操作仍仅一次，未验证不自动重写。此为实物首事务失败的受限重试，不是精度/稳定性验收。

0.4.5依据15只读对照：0x66在第1次超时后连续3次CRC正确且值1，不能据此前首次超时认定不支持；普通100ms周期47笔CRC正确。恢复严格地址寄存器前后验证，在维护每次请求前保留100ms保守空闲（最小可靠间隔及启动暂态原因未定，非通用性能指标）。前后只读各最多3次，写仍最多一次；用户确认温湿度外置电源12V，TTL模块仍3.3V，不关闭TBD电气验收。

## 当前实物地址与交付状态（2026-10-25，0.4.6）

用户确认温湿度外置电源12V；TTL仍3.3V。16-register-guard-commission在0.4.5下旧址0x66读回1；固定FC06仅写一次，写返回与请求相同，不能单独作为成功；随后2号0x66返回02030200027D85（CRC有效、地址及值均2），事件verified_1_to_2。17-post-success-readonly旧址1两次timeout、新址2首笔timeout后第二笔frame_ok，原码2630/5908，倍率未知故工程量null。原始归档和摘要已核对；未验证传感器断电保存、温湿度精度或辐照度共线。

0.4.6仅把主站envAddress默认值同步为已读回的2，不再发送设备写命令；所以此前章节工厂默认1及旧主站默认1属历史说明。INA226未接保持禁用；MAX31865 Rref/接线未验收，不自动启用。所有通道上电仍关闭。单独查看现有温湿度数据时按顺序发送set rs485_mode auto、enable env、start；结束stop。读回显示原码，倍率确认前不伪装温湿度工程量。

## 双从站轮询事务间隔（0.4.7）

用户恢复两只仪表A/B及外置电源后，19记录2号0x66连续4次读回2，支持本次用户报告的断电恢复后地址保持；并非重复寿命/长期保持验收。0.4.6双从站轮询中1号11次CRC有效、2号6次有效/5次timeout，事务间隔约10ms；不能把两路偶有数据当共线稳定。0.4.7将初始化、事务结束及未请求数据后的保守空闲统一为RS_QUIET_MS=100，维护亦使用同一值；保持9600/8N1、1/2地址与只读默认、单事务/原始帧/超时记录。每通道period_ms为目标轮询周期，受总线事务/空闲约束，不承诺任意设定均可达到。100ms为台架联调选择，最小需求和硬件根因尚未判定，不改变模块供电/引脚，不代表量程/保护/校准验收。

## PT100未补偿参考模式（0.5.0）

保留三根物理引线，新增停止状态命令 `set rtd_compensation 0`：关闭MAX31865配置D4的三线补偿，不伪称物理探头变成二线。默认该开关仍为1；1表示三线配置时请求补偿，不表示硬件已验收。428Ω来自用户断电在板表读，未作精密校准；不反推Rref，不除3修正原码。

当前单路台架查看命令（逐行；上电各通道仍关闭）：

```text
set rtd_wires 3
set rtd_compensation 0
set rref_ohm 428
enable rtd
start
```

有效读数状态为reference_uncompensated，rtd_wires仍为3，rtd_compensation_enabled=false，quality_flags包含LEAD_RESISTANCE_UNCOMPENSATED及UNCALIBRATED；保留原寄存器/原码/Rref、电阻和名义PT100 alpha=0.00385换算。它是含导线/接触/参考电阻误差的参考温度，不是三线补偿修复或精度验收。故障/量程/配置异常仍输出null，不为得到漂亮温度放宽检查。配置挥发，重启后需重新明确选择参考模式，不能自动把异常补偿读数当正常值。结束stop。

## 0.6.0本地中英文科研展板

用户授权本地Web展板后新增，支持**中文 / EN**一键切换，保留当前曲线及采集状态；标题、变量/轴名/图例、质量/统计、设置、错误与CSV表头同步翻译，原始JSONL不改字段。概念与约束见docs/WEB_DASHBOARD_SPEC.md。HTML源码在上级web/index.html；修改后运行`python3 firmware/pv-sensing/embed_dashboard.py`生成gzip头，再编译。

USB串口115200，输入`web`取得本次启动的热点SSID、随机密码和IP。手机/电脑连接PV-SenseLab热点，访问`http://192.168.4.1`；不需互联网。密码只在显式命令回复输出，不进入设备原始日志；复位后随机更新。网页默认不采集，按“开始采集”启用三个传感器四通道；INA226仍关闭，无负载/清洁控制或改址Web入口。

当前台架使用辐照度地址1、温湿度地址2、9600/8N1、TX18/RX17及100ms总线间隙；PT100保持物理三线、关闭补偿，参考电阻使用用户报告428Ω（未精密校准）。未知辐照度/温湿度倍率仍是0：页面显示原码count及状态，原始工程量null；停止后可明确填写实际倍率，程序不默认0.01。图形温度名称为PT100探头温度，不宣称电池结温。寿命预测所需电气/I–V、长期退化数据与终止准则尚缺，不输出虚构寿命。

趋势缓存1024点，会话均值/极值/样本标准差只计算真实有效值；n<2无标准差。数据质量图为有效/异常数量，非准确度证据。无效值断线，过期值明确标注。CSV只是已取得缓存；完整原始JSONL通过导出面板下载。Flash只追加，接近满停止，无自动覆盖/删除；未知非空文件系统不格式化，断电可丢失约1秒未flush尾部。下载后独立备份，长期科研采集继续用电脑归档工具。
