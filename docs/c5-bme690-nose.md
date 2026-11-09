# C5 刘看山的鼻子：BME690 基础接入

> 本文保留 2026-10-17 的接入记录。当前使用说明见
> [鼻子与 MCP](c5/nose-and-mcp.md)，2026-11-08 的实机结果和后续固件状态见
> [验证记录](c5/validation-and-limits.md)。最新鼻子配置不再启动 BMI270 任务。

本版先验证原配 BME690 子板的硬件与原始数据，提供语音查询入口。
代码能够编译不代表已确认实际传感器工作；最终需要串口自检和采样记录。

## 原配子板与总线

沿用 SensairShuttle 原有引脚，不新增或迁移 GPIO：SDA=2、SCL=3，
Shuttle CS=10 保持高电平、SDO=9 保持低电平，BME690 地址应为 `0x76`。
探测也检查 `0x77`，便于发现地址绑定位异常。原配子板供电跳线使用 3.3V。

CS/SDO 由板级初始化统一设置，先写输出锁存值再启用输出，避免 BMI270 的
CS 先低后高导致其锁定 SPI。两种传感器不再分别争用这些配置；触摸总线
保持原状。GPIO 配置失败只禁用可选传感器，不中止语音和显示。

触摸 `0x15`、BMI270 `0x68` 和 BME690 `0x76` 地址不同。原配主板的
Shuttle 接口用于更换不同子板：若只安装了 BME690 而卸下 BMI270 子板，
BMI270 的缺失警告和无法感知摇晃是预期现象，不能当成 BME690 故障。

## 采集与判断

使用组件管理器获取的官方 `espressif/bme690 ==1.1.0`，其中包含 Bosch
BME690 SensorAPI v1.0.3。项目只提供 ESP-IDF 原生 I2C 回调，不修改供应商
源码，不调用组件内的全局总线封装，也不重新创建/删除共享 I2C 总线。

1. 启动 1.5 秒后，在低优先级任务中探测地址。
2. 初始化校准参数，并同时核对芯片 ID `0x61` 与 BME690 variant `2`。
   仅看到 `0x61` 或地址 ACK 不足以确认 BME690。
3. 每次启动执行一次 Bosch 官方自检，串口输出 `Bosch self-test: PASS/FAIL`。
   自检会改变加热配置，完成后重新初始化并应用正常采样参数。
4. 每 3 秒触发一次 forced 测量，使用官方测试示例的 300°C/100ms 内部微加热器
   参数，读取温度、相对湿度、气压与气体电阻。这里是内部热板目标温度，
   不代表室温或外壳温度；长期运行参数可在实测后调整。
5. 核对新数据、有限数值和量程；气体值必须同时有 `gas_valid` 与
   `heater_stable`。无效数据不伪装为成功读数。
6. 连续五次读取失败后只移除本传感器的设备句柄，等待 10 秒重试。
   上一次数值超过 10 秒或读取失败时，MCP 不再返回它作为当前测量。

`hardware_ok=true` 要求：配置完成、自检通过、当前读数新鲜、气体与热板
状态有效，且已连续取得三次有效气体样本。这个标志只表示基础硬件采集
通过，不表示空气质量合格、长期漂移已消除或气味模型已训练。

串口记录前五个样本，之后每五个样本记录一次，包含有效位、累计错误与
任务栈余量。样例格式（并非实际测量结果）：

```text
Bme690Nose: Bosch self-test: PASS result=0
Bme690Nose: Configured: chip=0x61 variant=2 addr=0x76 ...
Bme690Nose: Sample=3 T=... RH=... P=... gas=... status=0xb0 gas_valid=1 heater_stable=1 ...
```

## “闻一闻”语音入口

板级注册 `self.pet.nose.get_status`，服务器支持设备 MCP 工具时，可通过
“看山，闻一闻”“鼻子正常吗”“现在温度湿度多少”获取最新缓存数据。
工具调用不等待测量周期，不操作 I2C，不抢占动画，也不主动打断播报。
模型是否选择该工具，仍需联网后的实际对话确认。

返回字段包括 `hardware_ok`、`self_test`、`fresh`、`sample_age_ms`、
`temperature_c`、`relative_humidity_pct`、`pressure_hpa`、有效时的
`gas_resistance_ohm`，以及采样/错误计数。无新鲜数据时省略数值字段，
避免把零、旧数据或无效电阻用于回答。尚未自检时返回 `not_run`。

当前 `iaq_available=false`、`odor_classification_available=false`。
气体电阻反映广谱响应，不能直接得出香臭、甲醛 ppm、真实 CO₂ 浓度或空气
安全结论。下一阶段可接入与 C5 架构匹配的 BSEC，以及针对目标场景训练的
气味模型，再用可靠结果触发嗅闻动作与漫画气泡；本版不虚构这些分类结果。

## 配置和资源

`CONFIG_USE_BME690_NOSE` 仅依赖 SensairShuttle 板型，和桌宠动画开关独立。
规范 C5 变体显式开启；关闭后不编译采集服务、不创建采集任务或注册鼻子工具。
驱动依赖只为 ESP32-C5 解析，不涉及 C3。仍只有原来的一个板级工厂。

新增一个优先级 1、4096 B 栈的任务和小型状态缓存；采样路径没有动态像素
或音频缓冲，没有新增 LVGL 对象、GIF 或素材包。加热等待用任务延迟让出
CPU。新增栈/TCB 与 I2C 设备存在内存开销，加热器增加供电负载，实际最低
SRAM、栈余量、语音并发与读数稳定性需上板确认。

## 本地验证记录（2026-10-17）

- ESP-IDF v6.1.0 的 C5 应用构建通过，应用 2,511,808 B，最小应用分区
  剩余约 39%；未构建 C3。
- 100 项主机测试通过。鼻子关闭/桌宠开启、鼻子开启/桌宠关闭、两者均关闭
  三种 C5 板级语法检查通过。
- 触及的 C/C++ 格式检查及 `git diff --check` 通过。日志位于
  `tmp/c5_bme690_20261017/`。
- 应用 SHA-256：
  `5f05d76053fbe6820adebfd0eaa3fc2572f38ea941ea6ce7f75aa138701498d3`。
- 素材包没有变化，仍为 5,381,890 B；不新增嗅闻动画或占用解码缓冲。
- 当前串口枚举为空，因此尚未烧录、未改写 NVS，尚无本机传感器自检、
  读数与语音工具调用的实测结论。USB 接通后继续这些验证。

此前位于系统临时目录的 IDF Python 环境有包文件缺失，本次使用官方
`idf_tools.py install-python-env` 在工程的
`tmp/c5_bme690_20261017/idf-python-env` 重建了 6.1 环境。复现构建时设置
`IDF_PYTHON_ENV_PATH` 指向该目录，再加载 `C:/esp/v6.1/esp-idf/export.ps1`；
已安装的编译工具仍使用 `IDF_TOOLS_PATH=C:/Espressif`。

## 参考

- [乐鑫 SensairShuttle 原配板说明](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp-sensairshuttle/user_guide_v1.0.html)
- [官方 BME690 驱动组件](https://components.espressif.com/components/espressif/bme690/versions/1.1.0/readme)
- [Bosch BME690 SensorAPI](https://github.com/boschsensortec/BME690_SensorAPI)
- [Bosch BME690 软件与训练工具](https://www.bosch-sensortec.com/en/software-tools/software/bme688-and-bme690-software)
