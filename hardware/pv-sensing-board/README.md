# PV 主采集板

状态：规格设计，尚无原理图/PCB/固件实物验证。

- [模块规格](MODULE_SPEC.md)：电源、计量、传感、控制及保护要求。
- [接口表](INTERFACES.md)：逻辑方向与未决电气参数。
- [schematics](schematics/README.md)：未来真实立创EDA原理图与审查入口。
- [pcb](pcb/README.md)：未来 PCB 源和布板门槛。

主控确定为 ESP32-S3；具体模组、供电、GPIO、ADC、连接器和 PCB 约束均 TBD。职责是可追溯采集与本地安全调度，不承受电子负载功率，不直接用 GPIO 驱动电机或 MOSFET 功率门极。

下一步：按根 [todo](../../todo.md) SCH-01–06 和 SCH-09/12 完成前置资料、接口、量程/误差/接地与保护设计，再绘制原理图。所有范围与状态以 [硬件总规格](../../docs/HARDWARE_SPEC.md) 为准。
