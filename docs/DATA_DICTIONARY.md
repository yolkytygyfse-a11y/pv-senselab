# 数据字典与追溯规则

版本：0.3；日期：2026-10-25；状态：正式 schema v1 草案；已有独立 Arduino 台架 profile/归档工具，已有台架原始通信与参考读数，尚无正式实验数据或校准结论。

## Arduino 台架记录与正式 schema 的区别

当前实现见 [Arduino 说明](../firmware/pv-sensing/siri/README.md)。profile=`pv-senselab-arduino/2`，记录 type 为 event/status/help/measurement；包含 device_id/boot_id/seq、t_mono_us、config_revision、dropped_records，以及状态/原码和可验证的换算结果。正式 run_id/calibration_id 未建立时明确 null，不能称正式实验有效样本；各事务时间是估计窗口，V/I 时差未知为 null。

UTC 未设为 null；设置 time 后的 utc_estimate_us 是近似值，host_declared_uncertainty_us 只回显操作者声明，clock_mapping_age_us 指映射年龄，总 utc_uncertainty_us 因延时/漂移尚未校准仍为 null。不能据此宣称符合正式时钟不确定度预算。

主机 capture_serial.py 保存 transport.bin（原样 RX）、commands.bin（实际发送字节）、records.jsonl（host_received_utc 与解析 record）、manifest.json（in_progress/complete/failed、字节/有效/无效行/部分尾行计数与摘要）。这是 capture_schema=1 的台架封装，不冒充下文实验 manifest。原始坏行和启动噪声原样保留；目录必须全新，数据文件只追加。正常退出才更新终态/摘要；强杀留下 in_progress，应恢复审查而非伪改成功。独立备份仍是 TBD-24。

## 文件与共同约定

每次运行独立路径 `data/raw/<experiment_id>/<run_id>/`：保存 `manifest.json`、`transport.jsonl`、`measurements.jsonl`、`events.jsonl` 及必要原始二进制附件。主机原始串口字节按有序分块保存并带接收时间，不丢弃无法解析的行。`measurements.jsonl` 是保留原码和换算值的解析记录；不能代替 `transport.jsonl` 中的原始证据。

派生产物另存于 `data/interim/`、`data/processed/`，标定存于 `data/calibration/`，组件/夹具/归档元数据存于 `data/metadata/`。实验计划及实际记录放 `experiments/protocols/` 和 `experiments/runs/`；路径以 ID 相互关联，不能只靠文件名猜测。

UTF-8；JSONL 一行一个 JSON 对象；数值只用有限值，无效/缺失为 `null` + 明确原因，不用 0、NaN、空串代替。CSV 只作带列定义的派生导出，空单元格映射 null，保留质量列和 schema_version。时间均注明 UTC，绝对时间使用 ISO 8601 `Z`，单调微秒为整数。科学量字段后缀标单位；不输出未定义的“精度分数”。

ID 为不含目录分隔符的非空字符串，创建后不复用。主机生成 experiment/run/scan/action/analysis ID，设备持久 device_id、每次启动新 boot_id，重连生成新 session_id。协议版本与数据 schema 版本分别管理，旧数据不可原地升级。

## 通用测量信封

| 字段 | 类型/单位 | 必填与含义 |
|---|---|---|
| schema_version | integer | 必填；初稿为 1，是格式决策而非实测值 |
| record_type | enum | 必填；iv_point / context_sample / load_health |
| device_id、boot_id、session_id | string | 必填；设备、启动、会话标识 |
| experiment_id、run_id | string | 实验采集必填；台架校准也必须有独立 run |
| seq | integer | 必填；设备本次启动输出流单调递增，含事件/响应；跳号须记录 |
| t_mono_us | integer / µs | 必填；采样参考时刻，不能用串口收到时刻替代 |
| sample_start_us、sample_end_us | integer / µs | 必填；相对 boot 的采样窗，end ≥ start |
| host_received_utc | string / UTC | 必填；主机接收时间，与采样时间不同 |
| sample_utc | string / UTC or null | 主机经映射导出；未同步为 null |
| clock_sync_id | string or null | 映射记录；未同步为 null |
| utc_uncertainty_us | number / µs or null | 绝对时间估计不确定度，不能假定为零 |
| calibration_ids | object | 通道名→标定 ID 或 null；未标定用 null 并标志，不伪造 ID |
| config_id、firmware_revision、hardware_revision | string | 必填；未知硬件修订不可伪造，登记未编号原因 |
| quality_flags | array[string] | 必填；无异常为 []；缺失原因可多项 |

V/I 单独窗字段覆盖通用联合窗；慢通道样本携带实际窗，没有真实转换时刻的仪表需加 `TIME_ESTIMATED`。

## I-V 点（iv_point）

| 字段 | 类型/单位 | 含义 |
|---|---|---|
| scan_id、point_index | string、integer | 点属于哪条曲线；index 从 0 开始且每曲线唯一 |
| measurement_plane、fixture_id | string | DUT 边界和损耗网络的登记引用 |
| sweep_direction、segment | enum | increasing_current / decreasing_current；open / sweep / near_short |
| setpoint_current_a | number / A | 命令值，绝不替代实测 |
| dac_code、raw_v_code、raw_i_code | integer or null | 原码，采样失败为 null+标志 |
| v_sample_start_us、v_sample_end_us | integer or null / µs | V 通道窗口；未知为 null 并标 TIME_ESTIMATED，不能拿读寄存器时刻冒充转换窗 |
| i_sample_start_us、i_sample_end_us | integer or null / µs | I 通道窗口，未知同上，不能据此判为同步合格 |
| vi_skew_us | number or null / µs | I 窗中心减 V 窗中心，可有符号；未知为 null，单独列为质量限制 |
| pv_voltage_v、pv_current_a | number or null / V、A | 对应标定换算的实测值 |
| pv_power_w | number or null / W | 由上述同一对 V/I 计算，标明派生 |
| u_voltage_v、u_current_a、cov_vi_va | number or null | 标准不确定度与协方差；未知不得填 0 |
| context_refs | object | 环境通道→sample_id 与 age_us，可空并标志 |
| settled、load_enabled | boolean | 稳定判据结果与负载状态 |
| load_temperature_c | number or null / °C | 保护温度及对应原始健康记录引用 |

## 环境与健康样本

`context_sample` 额外包含 `sample_id`、`channel`、`raw_value`（保留结构）、`value`、`unit`、`sensor_id` 和 `sensor_status`。PT100 原值包括 rtd_code、fault_register、线制、Rref 配置；Modbus 原值包括请求/响应原帧、寄存器数组、CRC 状态、转换配置版本。温湿度如共用转换，保留同一转换 ID。`load_health` 包含负载温度、原码、硬件 FAULT/断开反馈及心跳状态。

SAFE_IDLE 的只读校准也用 context_sample 表示单通道读数（可为 pv_voltage/pv_current），无 scan_id；保留参考源/仪表读数的单独关联记录。未标定转换值为 null，不允许以默认系数编造工程量，且不能启动负载扫描。

### 已有原型器件的数据保留

- **SM9569B 候选**：raw_value 保存 RS485 请求/响应、0x0000 的原始 16 位寄存器、CRC 与实际通信配置。厂家倍率未确认时 `value=null`、`unit=W/m²`、quality_flags 包含 SCALE_UNCONFIRMED；不因手册示例“假设 100”就产生工程量。转换配置须记录倍率/偏置及依据版本。
- **SM7820B**：environment_rs485 保留 slave_address/function_code/register_address/count、request_hex/response_hex、request_echo_bytes、raw_registers（失败null）、事务窗/status/exception_code。另记 env_profile、env_scale（未设null）、env_signed_temp_confirmed、temperature_status/humidity_status 与 temperature_c/relative_humidity_pct。raw模式、错误帧或映射不匹配时均不输出工程量；倍率未设为scale_unconfirmed；负温标准补码未确认时仅温度为encoding_unconfirmed，湿度可独立有效；越界分别为out_of_range。ok_uncalibrated仅表示换算通过，不代表标定/精度合格。工程字段布局保持profile /2，新增配置/状态字段。
- **INA226 原型**：`raw_v_code` 对应总线电压寄存器，`raw_i_code` 对应分流电压原码（不是芯片计算后的安培值）。关联记录保留模块分流值/不确定度、配置/校准寄存器及芯片电流/功率原寄存器；如寄存器时间窗不可确定则明确 TIME_ESTIMATED 和 vi_skew_us 未知，不虚构同步采样。写入芯片校准寄存器不等于完成实验室标定。
- **PT100/MAX31865**：用户已确认探头类型；仍保留线制、Rref、RTD 原码/故障与校准引用，不因类型匹配就默认温度有效。

## manifest、事件与闭环

| 记录 | 必需字段 |
|---|---|
| run manifest | schema_version、experiment_id/run_id、操作者、protocol_id、DUT/组件/fixture/传感器 ID、measurement_plane、开始/结束 UTC 及同步状态、软件/固件/EDA版本、config/calibration 引用、输入/输出文件清单与 SHA-256、记录数、丢包/无效计数、状态、终止原因 |
| scan summary | scan_id、run_id、原始文件/点范围、complete/aborted、valid/partial/invalid、方向、端点方法及资格、voc_v/isc_a/vmp_v/imp_a/pmax_w/ff（可 null）、离散/估计最大值区别、不确定度、算法/配置版本、质量标志 |
| event | event_id、设备/启动/会话和时间、seq（设备产生时）、源（host/device/hardware）、类型、severity、状态前后、request_id/scan_id/action_id、fault_code、detail；独立主机事件不伪造设备 seq |
| cleaning action | action_id、pre_scan_id、诊断分析 ID、人工授权操作者/时间、工艺/配置、开始/结束/限位与故障、结果、post_scan_id（尚未复测可 null） |
| recovery result | analysis_id、pre/post/baseline scan_id、action_id、环境匹配/归一化方法与资格、delta_p_w、relative_gain、loss_recovery（可 null）、不确定度/区间、拒绝原因、软件与配置版本 |

任务 manifest 状态：`in_progress`、`complete`、`aborted`。记录期间文件追加；关断后落盘并生成最终清单/摘要，冻结后不可改写。崩溃留下的 in_progress 后续以新恢复事件和新清单版本标注 aborted，保留旧文件，不伪造结束成功。元数据纠正使用有版本的补充记录，不能重写原始测量。

## 标定与时钟记录

标定记录包含 calibration_id、sensor/device/channel、量程/公式/系数及协方差、有效范围/时间、标准件和参考仪表、环境、原始校准文件摘要、残差、审批状态/人。原始 ADC 码必须可按旧系数重放，重处理时产生新 analysis_id。

时钟映射包含 clock_sync_id、device_id/boot_id/session_id、主机发/收 UTC 和单调时刻、设备接收/回复单调时刻、往返延时、映射参数/方法、不确定度及适用区间。时钟漂移或复位后新建映射；历史采样时间修正只发生在派生产物，不改串口原始记录。

## 拟合与诊断记录

拟合产物至少包括 analysis_id、源 scan_id/摘要、模型及代码版本、DUT 边界、参数化方式、Iph_a/I0_a/Rs_ohm/Rsh_ohm/a_v、Ns/n/T 取值及来源（未知为 null）、边界/先验、固定/自由参数、初值/随机种子、权重、收敛状态、残差与单位、点排除表、灵敏度/条件数、多初值结果、参数区间与辨识状态、模型适用性、失败原因。

辨识状态取 `identifiable`、`weakly_identifiable`、`unidentifiable`、`not_evaluated`；模型适用性独立为 `applicable`、`mismatch`、`not_evaluated`。不确定度未知不是参数为零。诊断记录保留标签来源、允许的多因素标签、证据、拒识原因、训练/验证划分与评价方法；未经校准的分值不能称概率。

## 质量标志与一致性

初稿标志：`UNCALIBRATED`、`OUT_OF_RANGE`、`ADC_SATURATED`、`SENSOR_FAULT`、`STALE_CONTEXT`、`TIME_UNSYNCED`、`TIME_ESTIMATED`、`VI_SKEW_EXCEEDED`、`NOT_SETTLED`、`PACKET_GAP`、`ENV_UNSTABLE`、`ENDPOINT_UNQUALIFIED`、`MODEL_MISMATCH`。使用前需定义触发阈值/恢复条件，阈值 TBD-14、16、20、22。未知标志保留并提示版本问题，不能删除。

补充：SCALE_UNCONFIRMED 在工程量比例/偏置尚无可信依据时触发；ENCODING_UNCONFIRMED 在帧映射/符号/无效值等无法可靠解释时触发。经厂家资料与受控验证批准相应配置后，新处理产物才可解除；旧原始记录不重写。二者均不得被当作合格模型输入。

原始数据允许物理越限值以记录故障；禁止把负值裁剪成零。校验器检查 schema、类型/单位、ID、时间顺序、point_index 唯一性、完整性清单、引用和摘要；物理质量检查单独给结果，不能因不理想而拒绝保存。备份与隐私位置规则见 [data](../data/README.md)。

台架 MAX31865 的 configuration_readback_failed 诊断须额外保存 expected_configuration、observed_configuration；不丢弃失败、不给温度值。首笔配置失败与后续原码分开记录；故障寄存器为0不等于整链测温合格。

维护事件 `env_address_setup` 包含 phase=read_old/write_once/verify_new、request_hex/response_hex、transport_status、echo_bytes；终止事件env_address_change包含verified/precheck_failed/outcome_unknown。verified必须由新地址的CRC有效地址寄存器读回支撑，不能由写应答或软件变量代替。原始控制台RX/TX与摘要一并保留。

2026-10-25维护协议修订0.4.2：SM7820B设备地址读取使用手册FA通用查询（FA0300660001719E），应答使用设备真实地址。read_old必须CRC有效且应答地址=1、寄存器值=1；verify_new必须应答地址=2、寄存器值=2。单仪表门禁不变；write_once仍固定010600660002E814，不写波特率/校正值。普通数据仍使用各自唯一从站地址。

0.4.3取代上述0.4.2维护读阶段：实物在两种0x66读取方式均无应答，普通温湿度读帧已验证。因此read_old_data发送010300000002C40B，verify_new_data发送020300000002C438，均须匹配目标地址、FC03、2寄存器、长度及CRC；验证成功事件改为verified_1_to_2_by_data。只证明当前新地址能读取数据，不证明地址寄存器内容、设备型号或断电保存。write_once固定帧和单仪表/一次写入门禁不变。

0.4.4补充：实际普通采集首查询超时而第二次成功，不能据首次无应答宣称0x66不受支持。read_old_data与verify_new_data各最多3次只读重试，每次250ms并保留每次原帧；前置未请求数据则停止；write_once仍最多一次，无自动写重试。维护最坏约2秒。

0.4.5现行维护行为取代0.4.3/0.4.4普通数据代验：15只读对照确认0x66支持且值1，恢复read_old=0103006600016415、verify_new=0203006600016426，均验证CRC/应答地址/寄存器值；成功事件verified_1_to_2。维护请求前保留100ms保守空闲，前后只读各最多3次，写仍仅一次，最坏约3秒。该间隔为台架诊断选择，未证明最小需求；断电保持未测。

0.4.6主站默认环境地址设为2，依据2026-10-25记录16中2号地址寄存器值2的CRC正确回复及17独立普通数据复查。仅更改主站默认，不新增设备写操作；出厂地址1的手册事实与新传感器首次配置流程不变，断电保存尚未验证。

0.4.7普通轮询空闲统一100ms（启动、事务结束、未请求字节后），与维护共用RS_QUIET_MS；波特率/地址/帧格式不变。period_ms仍为各通道目标周期，实际sample_start_us/sample_end_us记录真实时间，受共享总线占用约束。此次只读共线联调无设备地址写入，保留超时和未确认倍率null。

0.5.0 PT100参考模式：停止时set rtd_compensation 0/1，仅精确数字解析，默认1。rtd_wires保留声明的物理引线数，实际D4位由wires==3且compensation==1决定。3根引线且关闭补偿时，完整记录新增rtd_compensation_enabled=false，quality_flags增加LEAD_RESISTANCE_UNCOMPENSATED，换算有效status为reference_uncompensated，不称ok_uncalibrated或补偿合格。未知Rref仍null；硬件/数学故障优先输出错误。428Ω仅用户在板表读、无精密校准；物理线不变、无负载/清洁/地址写操作。现有profile/2只增加字段和标志，未知标志须保留。

## 展板派生数据与语言（0.6.0）

科学名称为太阳辐照度G、环境空气温度T_a、环境相对湿度RH_a、PT100探头温度T_RTD；对应id为irradiance、ambient_temperature、humidity、pt100。安装条件未确认，不将G冒称组件平面辐照度，不将T_RTD冒称结温/背板温度。倍率未知时前三项只显示原码count；PT100保留未补偿/未校准标志。

1024点环缓存与会话n/invalid/min/max/mean/std是可重建派生数据。点id为本启动单调游标，t_us为读取完成的设备单调微秒，std使用n−1。series_epoch在开始会话/配置变化时递增；浏览器隔离启动与epoch，不混画单位。故障值null、不填0，原帧/失败状态仍保留。

原始完整JSONL另存LittleFS的log-boot_id.jsonl，只追加。仅每字节FF空分区可初始化，未知非空分区不格式化；接近满停止，不覆盖日志。约每秒flush，断电可能丢失尾部，内部Flash不能替代独立备份。CSV仅为浏览器已取得有界缓存，中英文表头可切换；JSONL与协议字段稳定。当前没有可报告的退化或剩余寿命估计，不生成预测数值。
