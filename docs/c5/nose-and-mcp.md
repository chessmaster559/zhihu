# BME690 鼻子与设备端 MCP

[返回目录](README.md) · [硬件模式](hardware.md) · [实机证据](validation-and-limits.md)

## 当前“嗅觉”是什么

BME690 提供温度、湿度、气压和气体电阻。当前实现能验证传感器、取得原始
响应，并让模型查询这些读数；没有把某个电阻阈值命名成“香”“臭”或“咖啡”。

“可以闻到但无法分辨气味”符合当前能力，不说明传感器自检失败。单一加热
条件下的电阻值不能唯一确定气体类别；温湿度、混合物、自热和历史暴露也会
影响信号。把同一数值发送给更大的语言模型，不能补出缺失的化学指纹。

## 采集生命周期

实现位于 [bme690_nose.*](../../main/boards/espressif/esp-sensairshuttle/bme690_nose.cc)，
使用 `espressif/bme690 ==1.1.0` 包装的 Bosch BME690 SensorAPI v1.0.3。

1. 板级一次性设置共享 CS/SDO；优先级 1、4096 B 栈任务在启动 1.5 秒后工作。
2. 探测 `0x76/0x77`，核对 chip ID `0x61` 和 BME690 variant `2`。
3. 本次启动执行一次 Bosch 自检；自检结束重新初始化，恢复正常测量配置。
4. 每 3 秒触发 forced 测量，内部热板目标 300°C、持续 100 ms，温度 2×、
   压力 1×、湿度 16× oversampling。加热等待让出 CPU，不阻塞音频任务。
5. 只接收新数据与有限、量程内的温湿压值；气体额外核对 `gas_valid`、
   `heater_stable` 和正有限电阻值。
6. 连续五次失败后移除本传感器设备句柄，等待 10 秒重新初始化，保留共享总线。

自检和加热初期的热板无效样本不计入有效气体计数。官方自检也有环境条件
约束，失败应结合原始状态和环境检查，不能只凭一个 FAIL 宣称传感器损坏。

## 可用性判据

`hardware_ok` 同时要求：配置完成、自检 PASS、最后样本不超过 10 秒、
气体与加热状态有效，且连续三个气体样本有效。失败立即撤销当前有效标记。
该标志证明基础采集，不证明完成 IAQ 校准、气味训练或空气安全评估。

温度为传感器测量值，不是经过整机自热补偿的标准室温；气体电阻为 Ω，不是
AQI、IAQ 或 ppm。本轮没有增加 BSEC 库、训练配置或伪造 CO₂/甲醛数值。

## 工具契约

工具名：`self.pet.nose.get_status`，无参数。板级注册，未启用鼻子时不提供。
缓存复制受互斥锁保护，工具不等待测量周期。`cJSON*` 结果交给现有 MCP 层
管理所有权，分配失败返回工具错误。

请求格式：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {"name": "self.pet.nose.get_status", "arguments": {}}
}
```

| 字段 | 含义 |
| --- | --- |
| sensor / detected / configured | 名称、型号识别及配置状态 |
| hardware_ok / fresh / sample_age_ms | 基础健康、数据新鲜度、样本年龄 |
| self_test / self_test_code | not_run、pass 或 fail，以及 Bosch 返回码 |
| i2c_address / chip_id / variant_id | 数值形式的地址及身份，不是十六进制字符串 |
| gas_valid / heater_stable | 当前新鲜样本的气体与热板有效位 |
| samples / valid_gas_samples / consecutive_valid_gas / errors | 样本、有效气体、连续有效与错误计数 |
| temperature_c / relative_humidity_pct / pressure_hpa | 新鲜有效样本的工程单位读数 |
| gas_resistance_ohm | 仅在新鲜且气体/热板有效时提供 |
| iaq_available / odor_classification_available | 当前均为 false |
| interpretation | 明确原始气体响应的解释边界 |

无新鲜数据时省略测量数值，不用 0 或上一轮残值冒充当前结果。实际查询会
打印 `Bme690Nose: Query: hardware_ok=...` 摘要；启动时 `Add tool` 只证明
注册成功，不能证明服务器实际调用。完整语音链路还依赖联网、ASR、服务器
工具调度和 TTS，不能用传感器 PASS 代替它。

## 后续气味分类路线

目标应先限定少量类别，例如空白环境和两种指定样品，而不是“识别所有气味”。
可按 [Bosch 官方软件流程](https://www.bosch-sensortec.com/en/software-tools/software/bme688-and-bme690-software)
采集多加热条件下的数据，记录温湿度与标签，分别划分训练/验证/跨天测试集。
样本不足或只在同一时刻随机拆分，会高估泛化能力。

训练后需确认 BME690/BSEC 版本、导出配置、ESP32-C5 RISC-V ABI 和工具链
支持，再量测 Flash、RAM、任务栈、耗电及语音并发。BSEC 是带许可条件的
厂商库，当前仓库未包含该二进制。也可以设计经验证的自有特征与分类器，
但应保留“未知/低置信度”输出，不能把气体电阻阈值包装成完成的识别模型。

完成这些步骤后，才将分类结果映射到嗅闻动作、漫画气泡和语音；保留原始
读数、模型版本及置信度，避免仅凭语言模型描述猜测气味。
