# 本地科研软件

状态：已有标准库原始归档 CLI [capture_serial.py](capture_serial.py)，适用于 macOS/Linux 的明确串口；用法 `python3 software/capture_serial.py --help`。尚无科研分析 Python 包、Web 或正式 API 实现。

- [src/pv_senselab](src/pv_senselab/README.md)：后续实际代码入口，负责有线会话、原始归档、实验编排、质量检查和特征提取。
- [configs](configs/README.md)：后续经过评审的配置与版本说明。

优先 Python 标准库；串口、数值优化等确有需要才选最小依赖并固定兼容版本（TBD-25）。不提前搭数据库、前后端、消息中间件或复杂插件框架。模型计算与硬件控制通过文件/明确函数边界协作，不复制物理公式。

编码前阅读 [协议](../docs/API_PROTOCOL.md)、[数据字典](../docs/DATA_DICTIONARY.md) 和 [扫描规格](../docs/IV_TRACER_SPEC.md)。当前归档工具不覆盖旧目录，保存准确 RX/TX 字节、主机接收时间和 SHA-256；配置发送仅来自操作者明确提供的命令文件。解析坏行/非有限值/重复键保留在原始字节中。文件完整性测试已实现；真实串口通信未运行。台架记录不能冒充已满足正式实验 schema/标定条件。
