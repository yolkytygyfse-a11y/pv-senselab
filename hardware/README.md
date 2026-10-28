# 硬件工程

当前仅模块规格与逻辑接口；**没有已绘制原理图、PCB、制造文件或上电结果**。使用立创EDA，版本和器件库 TBD-18。

| 模块 | 内容 | 下一门槛 |
|---|---|---|
| [pv-sensing-board](pv-sensing-board/README.md) | ESP32-S3、V/I、温度、环境、RS485、预留口及控制 | 量程/供电/器件/接地/pinout 冻结 |
| [pv-electronic-load](pv-electronic-load/README.md) | 独立 DAC/模拟恒流环、线性 MOSFET、散热与保护 | 线性 SOA/热预算、故障断开与端点方案评审 |
| [cleaning-module](cleaning-module/README.md) | 清洁机构、驱动、反馈及互锁规格 | 干湿工艺/机械/堵转与电源方案冻结 |

设计前阅读 [总规格](../docs/HARDWARE_SPEC.md)、[传感器](../docs/SENSOR_SPEC.md)、[扫描](../docs/IV_TRACER_SPEC.md) 和 [BOM](../docs/BOM.md)。两板逻辑信号须互相一致；连接器物理 pinout 未冻结不得接线。所有板上测试点都应覆盖校准注入和故障排查。

原理图完成须提交真实 EDA 源、版本化 PDF/网表与审查；PCB 完成须有源文件、DRC/人工审查和制造依据。不得仅添加空扩展名文件表示完成。上电前检查量程、SOA、热耗散、接地、保护和校准；始终禁止屋顶光伏接入。
