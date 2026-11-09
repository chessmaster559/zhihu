# C5 主板与原配传感器子板资料

[工程文档](../c5/README.md) · [本项目硬件与接线说明](../c5/hardware.md)

这里附厂商原版原理图/PCB 的官方入口、版本、页数和完整性清单；项目自行编写
的硬件技术文档位于上一链接。检索和下载校验日期为 2026-11-09。

PDF 本地副本不提交 Git。仓库提供原始来源与校验信息，可用脚本按清单下载；
厂商资料保留原版权，不将第三方 PDF 重新声明为本项目 MIT 软件许可证内容。

## 主板及子板原理图、PCB

| 资料 | 版本 | 页数 | 官方 PDF |
| --- | --- | ---: | --- |
| ESP-SensairShuttle 主板原理图 | V1.0，文件日期 2025-12-16 | 3 | [下载](https://dl.espressif.com/AE/esp-dev-kits/SCH_SCH-ESP-SensairShuttle-MainBoard-V1_0_2025-12-16.pdf) |
| 主板 PCB 布局 | V1.0，2025-12-16 | 6 | [下载](https://dl.espressif.com/AE/esp-dev-kits/PCB_PCB-ESP-SensairShuttle-MainBoard-V1_0_2025-12-16.pdf) |
| BME690 子板原理图 | V1.0，2025-12-16 | 1 | [下载](https://dl.espressif.com/AE/esp-dev-kits/SCH_SCH-ShuttleBoard-BME690-V1_0_2025-12-16.pdf) |
| BME690 子板 PCB | V1.0，2025-12-16 | 4 | [下载](https://dl.espressif.com/AE/esp-dev-kits/PCB_PCB-ShuttleBoard-BME690-V1_0_2025-12-16.pdf) |
| BMI270/BMM350 子板原理图 | V1.1，2025-12-16 | 1 | [下载](https://dl.espressif.com/AE/esp-dev-kits/SCH_SCH-ShuttleBoard-BMI270%26BMM350-V1_1_2025-12-16.pdf) |
| BMI270/BMM350 子板 PCB | V1.1，2025-12-16 | 4 | [下载](https://dl.espressif.com/AE/esp-dev-kits/PCB_PCB-ShuttleBoard-BMI270%26BMM350-V1_1_2025-12-16.pdf) |

这些地址来自[官方 ESP-SensairShuttle V1.0 用户指南](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp-sensairshuttle/user_guide_v1.0.html)。
指南中 BMI270/BMM350 链接文字与文件名版本存在差别，本清单按实际下载文件
标注 V1.1。实际使用仍需核对 PCB 丝印；本清单不证明不同硬件修订完全等价。

主板原理图第 1 页为框图，第 2 页为 MCU/音频，第 3 页为供电和接口。
子板原理图分别核对 CS/SDO、SDA/SCL、供电和 BMM350 地址绑定位。
已检查上述 6 个 PDF 能解析、共 19 页；校验信息见 [sources.json](sources.json)。

## 芯片、屏幕和软件原始资料

| 内容 | 官方入口 |
| --- | --- |
| ESP32-C5 芯片数据手册 | [PDF](https://documentation.espressif.com/esp32-c5_datasheet_en.pdf) |
| ESP32-C5-WROOM-1 / 1U 模组 | [资料页](https://documentation.espressif.com/esp32-c5-wroom-1_wroom-1u_datasheet_en.html) |
| 1.83 寸 P183B001-V4-CTP 显示屏规格 | [PDF](https://dl.espressif.com/AE/esp-dev-kits/1.83-inch-LCD-P183B001-V4-CTP.pdf) |
| BME690 数据手册 | [PDF](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme690-ds001.pdf) |
| BMI270 数据手册、驱动 | [Bosch 产品页](https://www.bosch-sensortec.com/en/products/motion-sensors/imus/bmi270) |
| BMM350 数据手册、驱动 | [Bosch 产品页](https://www.bosch-sensortec.com/en/products/motion-sensors/magnetometers/bmm350) |
| BME690 SensorAPI | [Bosch 源码](https://github.com/boschsensortec/BME690_SensorAPI) |
| BME690 的 ESP-IDF 组件 | [espressif/bme690 1.1.0](https://components.espressif.com/components/espressif/bme690/versions/1.1.0/readme) |
| BSEC / BME AI-Studio | [Bosch 官方软件入口](https://www.bosch-sensortec.com/en/software-tools/software/bme688-and-bme690-software) |

数据手册、BSEC 库和训练工具的版本/许可由厂商维护，不因本仓库提供链接而
代表已集成或适用于当前 C5 工具链；当前鼻子只集成 SensorAPI 基础采集。

## 本地下载与校验

在工程根目录执行，不需要额外 Python 库：

```sh
python scripts/fetch_c5_hardware_docs.py --list
python scripts/fetch_c5_hardware_docs.py
python scripts/fetch_c5_hardware_docs.py --verify-only
```

默认保存到本目录的 `originals/`，该目录被 Git 忽略。脚本逐个核对 HTTPS 来源、
PDF 文件头、字节数和 SHA-256，下载后才原子替换临时文件；已存在但校验不符
的文件不会被静默覆盖。官方文件发生变化时，先人工核对版本，再更新清单。

原始文件名和最终 URL 均保留在 `sources.json` 中。本清单只是技术资料来源
管理，不是 PCB 电气设计审核或可靠性认证。
