# 独立可编程电子负载板

状态：规格设计；尚无原理图、PCB、SOA/热测试或扫描结果。

- [模块规格](MODULE_SPEC.md)：本地恒流环、端点约束、散热和硬件关断。
- [接口表](INTERFACES.md)：与主采集板对称的逻辑定义。
- [schematics](schematics/README.md) 与 [pcb](pcb/README.md)：未来真实 EDA 源目录及门槛。

负载板吸收 PV 能量，模拟闭环本地运行；暂不增加第二 MCU。DAC、运放、MOSFET、分流、供电、功率/持续时间额定值均 TBD。不得因为组件标称 20W 就宣称负载可安全持续吸收任意工况。

下一步执行 [todo](../../todo.md) SCH-02/03/04/07/08/09/12，先冻结包络与保护再画原理图。
