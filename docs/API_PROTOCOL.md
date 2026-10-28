# 接口与通信协议

版本：0.3；日期：2026-10-25；状态：正式协议仍为待评审设计，当前 Arduino 仅实现下文独立台架 profile，不声称已有完整 API。SM9569 手册默认值未经实物通信验证；硬件及吞吐仍待验收。

## 已实现的 Arduino 台架 profile

profile=`pv-senselab-arduino/2`：输入是有界 ASCII 命令（191 字节内，以 LF 结束），输出是 JSONL。实现 status/help、start/stop、enable/disable、严格 set 配置和主机 time 映射，不实现下文正式 JSON command/response、ARM、扫描或清洁。具体命令、P1、编译配置和状态见 [Arduino 说明](../firmware/pv-sensing/siri/README.md)。

控制台默认 115200，单输出行缓冲 2048 字节、队列 8 行；程序分片发送，过载停止采样并暴露 seq 缺口/dropped_records，需明确 start 恢复。所有传感器上电关闭；start 的已启用总线初始化须全部成功，否则清理并保持停止。复位丢弃配置，避免未经确认的参数自动复用。手册地址/速率仅为候选，RS485 方向、RTD 线制、分流、Rref、倍率/环境仪表编码按说明逐项确认。

time 只建立主机声明的近似 UTC 映射，utc_estimate_us 与 host_declared_uncertainty_us、mapping age 分开，总 utc_uncertainty_us 未校准仍为 null。未实现正式实验任务/标定 ID，run_id/calibration_id 为 null。所有数据均带未标定/时间估计标志；正式协议以下各节继续作为未来要求，不能把接口描述当完成功能。

## 接口边界

| 接口 | 角色与传输 | 当前约定 | 未决项 |
|---|---|---|---|
| Python ↔ ESP32-S3 | 主机发命令；设备发响应/测量/事件；有线 USB 串口 | UTF-8 JSONL，protocol_version=1 | 原生 USB/桥接、串口配置、帧长/缓冲与吞吐 TBD-07、26 |
| 主板 ↔ 电子负载 | 主板控制 DAC；负载本地模拟闭环与保护 | SPI 设定 + ENABLE/FAULT/心跳 + 健康回读；无第二 MCU | 型号、物理电平/极性、GPIO、连接器、是否隔离 TBD-05、10、13 |
| 主板 ↔ 辐照度仪 | 主板唯一 Modbus 主站，仪表从站 | SM9569B 家族手册支持 RS485 Modbus RTU，只读原型 | 手册 DC12–24V；实际地址/速率、倍率、线序/TTL 电气及响应待核对 TBD-09、10 |
| 主板 ↔ 环境温度仪 | SM7820B RS485 Modbus RTU | 默认9600/8N1、地址1、FC03/PDU0/双寄存器；共用UART1 | 实物配置、电源预算/线序核对、负温/倍率 TBD-04、08、10 |

SPI/硬件信号不是 JSON 协议；Modbus 不用本项目命令格式。不得臆造厂家寄存器。逻辑接口表位于两板的 INTERFACES.md。

两个 RS485 仪表只有在物理层、电源/参考地、串口参数和应用协议均审查通过后才能共线。当前单事务轮询；地址冲突拒绝启动。接在总线上的每个实物从站均须唯一地址，不能仅靠 disable 避免冲突。

SAFE_IDLE 允许定期发送安全监测及只读校准样本，未标定通道原码仍可归档，换算值为 null 并带 UNCALIBRATED。此模式不需要 ARM、不能驱动负载或清洁；初始校准以独立审查的参考源/无源夹具完成，避免“没有标定就无法取得校准原码”的循环依赖。

## 串口消息信封

一行一个对象，LF 结束，不发送 NaN/Infinity。所有消息含 `protocol_version`、`type`；不兼容版本拒绝 ARM。正式解析前必须限制长度/深度并拒绝重复 JSON key；限制由 TBD-26 冻结。启动日志/调试输出不能混入协议通道。

| type | 字段 |
|---|---|
| command | request_id、session_id（hello 除外）、command、payload；活动命令另带 arm_token |
| response | request_id、device_id、boot_id、session_id、seq、t_mono_us、status、payload 或 error |
| measurement | device_id、boot_id、session_id、seq、t_mono_us、payload（对应 DATA_DICTIONARY 的测量字段） |
| event | device_id、boot_id、session_id、seq、t_mono_us、event、payload（含关联任务及状态） |

设备 seq 在同一 boot 内所有输出帧递增，连接重建不清零；新 boot 用新 boot_id 区别。主机不把自己接收/解析事件强塞到设备 seq 中。`response.status` 为 `accepted`、`ok` 或 `error`；accepted 仅代表任务接受，scan/action 完成必须有终止事件，不能把 ACK 当成功结果。

## 命令语义

| command | payload / 前提 | 响应与行为 |
|---|---|---|
| hello | 主机支持版本、主机标识；输出保持安全 | 返回能力、device_id/boot_id、新 session_id、固件/硬件/配置版本及当前状态；不隐式复位故障 |
| get_status | 空对象 | 状态、保护/传感有效性、活动 ID、限制及队列状态 |
| sync_clock | sync_id、主机发送 UTC/单调时间 | 回显 ID，返回设备收/发单调时刻；主机记收时刻并生成 clock_sync_id |
| configure | config_id、experiment_id/run_id、标定引用、测量边界、扫描配置及安全限制 | 仅 SAFE_IDLE；逐项校验能力和冻结边界，返回实际接受配置摘要；未知键或越限拒绝。只读校准可保留空标定引用，ARM 仍必须拒绝未标定任务 |
| arm | config_id/摘要、任务类型 scan/clean、外部存储就绪确认 | 自检/互锁通过后返回一次任务 arm_token；授权限本会话/boot，超时 TBD-16 |
| heartbeat | session_id，活动时 arm_token | 更新安全租约，返回设备状态；超时硬件/固件各按冻结设计停机 |
| start_scan | scan_id、arm_token、已校验扫描配置引用 | ARMED(scan) 且 ID 全新；accepted 后异步发点和 scan_finished/scan_aborted |
| start_clean | action_id、pre_scan_id、诊断引用、人工授权记录引用、arm_token | ARMED(clean)、负载断开反馈有效、清洁互锁通过；accepted 后发动作事件和终止事件 |
| stop | 原因、关联任务 ID（可空） | 任意状态可请求；立即安全停止，活动任务 aborted；重复 stop 无副作用；不是物理急停替代 |
| reset_fault | 人工确认与原因记录 | 仅故障解除且物理许可有效；清除允许的锁存并重新自检，绝不自动恢复任务 |

2026-10-25用户授权ESP32本地传感展板，受限HTTP接口见本文件末尾；不提供远程裸门极控制或运行中任意set_current命令。台架调试需另写受限规程，不可通过配置绕过保护。清洁授权引用是审计信息，不是安全认证机制；运行仍依赖物理限位和硬件许可。

## 重试、幂等与断联

request_id 在会话内唯一。相同 ID/相同内容返回缓存响应或已知任务状态，绝不重执行扫描/清洁；相同 ID/不同内容返回冲突。新连接使旧 arm_token 失效；新 boot/丢失去重历史后不得自动重试活动命令。主机先查询状态，未知结果标为未确认/aborted 并人工核对；不以新 ID 自动重复动作。

去重缓存容量/保留期与帧上限 TBD-26；缓存淘汰后遇到无法判定的新旧请求，应拒绝活动命令并要求重新授权，不能猜测。会话 hello 的重放也不得替换现有活动会话，活动中重连先安全停止。

串口序号只检测遗漏/重复，不保证原始字节完整；主机保存原始分块，必要的校验/分块摘要见数据字典。主机持久化失败或无法保持消费速率须 stop；设备有界缓冲不能静默丢点，溢出中止。设备掉电可能丢失 RAM 中尚未发送点，明确记录覆盖限制，不声称零丢失。速率与背压预算冻结后才能宣称满足扫描吞吐。

## 错误与事件

错误码包括 `UNSUPPORTED_VERSION`、`INVALID_ARGUMENT`、`LIMIT_NOT_CONFIGURED`、`OUT_OF_RANGE`、`NOT_CALIBRATED`、`INVALID_STATE`、`INTERLOCK_OPEN`、`SENSOR_FAULT`、`LOAD_FAULT`、`REQUEST_CONFLICT`、`TOKEN_EXPIRED`、`BUFFER_OVERFLOW`。返回可读原因、字段位置及当前安全状态；错误处理不得输出伪测量值。

设备事件至少有 state_changed、scan_started/finished/aborted、clean_started/finished/aborted、fault_latched、clock_reset、packet_or_buffer_fault。终止事件必须包含实际点数、结束原因、关断反馈与最终状态。

## Modbus 与板间实现门槛

### SM9569 手册的只读候选配置

依据用户提供的《SM9569 产品使用手册》V23.10.14 p.5–6，RS485 型对应 SM9569B；非记录仪表默认 9600 baud、8 数据位、无校验、1 停止位，地址 1。它们是出厂默认说明，不能覆盖实物已改配置。读取辐照度使用 0x03，起始 PDU 地址 0x0000，数量 1，对应组态地址 40001。首个寄存器为 16 位原值，数据字节按手册高字节在前解释，工程倍率尚未确认。

手册只读请求示例为 `01 03 00 00 00 01 84 0A`。2026-10-25 已离线重算该示例及手册回复示例的 Modbus CRC，均匹配；这只证明示例字节的 CRC 一致，未向设备发送或取得任何测量。回复须检查地址/功能码/字节数/长度/CRC；0x83 异常响应单独记录。

p.5 的“假设倍率为 100”不能作为固定换算规则。倍率未确认时保留原帧和寄存器，工程值 null，使用 SCALE_UNCONFIRMED 标志；不把手册回复示例当实验读数。p.6 的 250 地址查询属于厂家特殊机制，不当作 Modbus 通用广播或多从站扫描方法。本期先做普通地址的只读验证，不因手册含写操作就主动修改地址、波特率或校正值。

接线前核对实物 B 型后缀、DC 电源、RS485 A/B/参考地与模块隔离/TTL 电平、自动方向或 DE/RE 控制。GPIO、线色和模块针号均未冻结，不生成接线即用配置；独立实验供电不能与 MCU 信号引脚混接。

### SM7820B 双寄存器读取与共线约束

依据用户提供 V24.3.19 p.2、7–8：默认9600/8N1、地址1、FC03、PDU0x0000温度及0x0001湿度，查询数量2。请求示例 01 03 00 00 00 02 C4 0B。实际设备配置另核对；原CAN版资料不采用。

台架 profile /2 的 enable rs485 为 SM9569，enable env 为 SM7820B。SM7820B 的默认候选映射已设置，但所有通道仍默认关闭。env_profile=sm7820b 仅在 FC03/PDU0/数量2 时解码；raw 模式可用通用 FC03/04 读取1–8字并保留 null 工程量。env_scale 为显式每 count 系数（未设置为 null），env_signed_temp 默认0，对0x8000–0xffff保留 ENCODING_UNCONFIRMED；确认标准16位补码后设1才按 raw−65536 换算。手册 /100 的假设措辞及减65535错误见 SENSOR_SPEC，未做实测确认。

回复严格检查地址/功能/字节数/长度/CRC/异常，分别输出 temperature_status/humidity_status、temperature_c/relative_humidity_pct；原帧/寄存器/配置均保留。CRC失败、超时、取消不沿用旧值；映射不匹配拒绝解码。没有仪表地址/波特率/校正写操作。

两仪表共 UART1，一条请求在途、完成后静默再轮询；相同通信参数且所有实物从站地址唯一。两本手册默认均为地址1，须先分别连接、用厂家规程核实/设置不同地址，再合并；同址拒绝 start 不代替实物配置检查。字段限制与具体命令见 Arduino 说明。

Modbus RTU 的帧间隔、CRC、主从访问和异常响应依据 [Modbus 官方规范](https://www.modbus.org/modbus-specifications)，具体设备映射来自厂家手册。RS485 终端/偏置依据总线拓扑设置，避免每个模块重复偏置；A/B 命名须按实物验证，不能仅凭丝印假定极性。

板间 SPI 必须校核上电 DAC 输出、掉电回灌、CS 默认态、驱动能力与线缆长度；ENABLE 故障默认禁止吸收，FAULT 断线可诊断，独立心跳超时能关断。逻辑极性和参数 TBD-10、16；关闭前不得连接实物。

未来协议验证覆盖分帧/粘包、坏行/超长/重复 key、不兼容版本、非法配置、重复请求、乱序/丢帧、复位/断联、满缓冲、主机磁盘失败、清洁重复授权与 Modbus CRC/超时。当前没有实现或测试通过记录。

## 受限温湿度改址维护命令（0.4.0）

用户明确授权且确认单独连接SM7820B后，提供 `commission_env_1_to_2 single_device`。仅停止状态、所有采集通道关闭、rs485_mode已指定且输出队列空闲时执行；固定9600/8N1，先FC03读取1号设备PDU0x0066并核对返回1，再仅发一次FC06写该寄存器为2，最后在地址2读同寄存器核对返回2。手册依据SM7820B V24.3.19 p.8，写帧010600660002E814。不提供任意寄存器写、波特率/校正写、广播或自动重复写。每阶段有界超时、原始收发/状态保留；只有新地址读回匹配才verified并更新主站env_address，写后无回复或不匹配标outcome_unknown，不重写。该命令无法电子识别是否仅接一只仪表，single_device是操作者的物理隔离确认。维护最长约1秒，停止态不驱动其他接口。普通采集仍只读。

2026-10-25维护协议修订0.4.2：SM7820B设备地址读取使用手册FA通用查询（FA0300660001719E），应答使用设备真实地址。read_old必须CRC有效且应答地址=1、寄存器值=1；verify_new必须应答地址=2、寄存器值=2。单仪表门禁不变；write_once仍固定010600660002E814，不写波特率/校正值。普通数据仍使用各自唯一从站地址。

0.4.3取代上述0.4.2维护读阶段：实物在两种0x66读取方式均无应答，普通温湿度读帧已验证。因此read_old_data发送010300000002C40B，verify_new_data发送020300000002C438，均须匹配目标地址、FC03、2寄存器、长度及CRC；验证成功事件改为verified_1_to_2_by_data。只证明当前新地址能读取数据，不证明地址寄存器内容、设备型号或断电保存。write_once固定帧和单仪表/一次写入门禁不变。

0.4.4补充：实际普通采集首查询超时而第二次成功，不能据首次无应答宣称0x66不受支持。read_old_data与verify_new_data各最多3次只读重试，每次250ms并保留每次原帧；前置未请求数据则停止；write_once仍最多一次，无自动写重试。维护最坏约2秒。

0.4.5现行维护行为取代0.4.3/0.4.4普通数据代验：15只读对照确认0x66支持且值1，恢复read_old=0103006600016415、verify_new=0203006600016426，均验证CRC/应答地址/寄存器值；成功事件verified_1_to_2。维护请求前保留100ms保守空闲，前后只读各最多3次，写仍仅一次，最坏约3秒。该间隔为台架诊断选择，未证明最小需求；断电保持未测。

0.4.6主站默认环境地址设为2，依据2026-10-25记录16中2号地址寄存器值2的CRC正确回复及17独立普通数据复查。仅更改主站默认，不新增设备写操作；出厂地址1的手册事实与新传感器首次配置流程不变，断电保存尚未验证。

0.4.7普通轮询空闲统一100ms（启动、事务结束、未请求字节后），与维护共用RS_QUIET_MS；波特率/地址/帧格式不变。period_ms仍为各通道目标周期，实际sample_start_us/sample_end_us记录真实时间，受共享总线占用约束。此次只读共线联调无设备地址写入，保留超时和未确认倍率null。

0.5.0 PT100参考模式：停止时set rtd_compensation 0/1，仅精确数字解析，默认1。rtd_wires保留声明的物理引线数，实际D4位由wires==3且compensation==1决定。3根引线且关闭补偿时，完整记录新增rtd_compensation_enabled=false，quality_flags增加LEAD_RESISTANCE_UNCOMPENSATED，换算有效status为reference_uncompensated，不称ok_uncalibrated或补偿合格。未知Rref仍null；硬件/数学故障优先输出错误。428Ω仅用户在板表读、无精密校准；物理线不变、无负载/清洁/地址写操作。现有profile/2只增加字段和标志，未知标志须保留。

## ESP32本地传感展板API（0.6.0）

仅SoftAP本地HTTP，默认192.168.4.1；无互联网服务。热点随机密码通过显式串口`web`获取，不进入原始日志。中英文切换改变展示和CSV表头，原始JSONL字段与协议保持稳定。详见[展板规格](WEB_DASHBOARD_SPEC.md)。

| 方法/路径 | 内容与边界 |
|---|---|
| GET / | gzip离线页面 |
| GET /api/state | version/device_id/boot_id/streaming/mono_us/free_heap/config_revision/token/ap/storage/config/cursor/max_points/series_epoch/channels |
| GET /api/history?after=N&limit=64 | 最多64点/页；返回epoch/oldest/cursor/lost/more/points；1024点RAM展示缓存 |
| POST /api/start | 台架已知地址1/2、9600/8N1、PT100物理3线未补偿参考模式，INA关闭；新派生会话 |
| POST /api/stop | 中止在途读取并关闭偏置/总线；原始日志保留 |
| POST /api/config | 停止后原子提交period_ms(1000–60000)、rref_ohm(350–10000)、irradiance_scale/env_scale(0–100，0未知)；URL编码四字段；清派生缓存 |
| GET /api/files | files数组含name/bytes/current；只列log-16位十六进制启动ID.jsonl |
| GET /api/raw?name= | 严格文件名下载完整JSONL，无删除接口 |

POST需同源state的随机`X-PV-Token`，不启用CORS。GET不执行控制。成功返回`{"ok":true}`，失败非2xx并返回`{"ok":false,"error":"稳定英文代码"}`。页面翻译说明，代码不作自由执行入口。仅配置换算不等于标定。

channels顺序为辐照度、环境空气温度、环境相对湿度、PT100探头温度；含id/unit/value/raw/status/last_us/stats。points含id/channel(0–3)/t_us/value/raw/status。倍率未知且帧有效时value为原码、unit=count/status=scale_unconfirmed，串口/Flash工程量仍null。stats含n/invalid/min/max/mean/std，样本标准差分母n−1，n<2为null。原码有效不等于已校准，单调时间不等于UTC。
