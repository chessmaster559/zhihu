# 构建、素材与烧录

[返回目录](README.md)

## 依赖与 SDK

使用 ESP-IDF v6.1，最低 v6.0.1；SDK 5.x 不支持。先安装对应工具链及 Python
依赖并加载该 SDK 的环境，再确认 `idf.py --version`。不要把 IDF 6.0 的 Python
虚拟环境直接用于 IDF 6.1。具体依赖版本以 [main/idf_component.yml](../../main/idf_component.yml)为准。

关键组件为 LVGL `~9.5.0`、官方 EAF 播放器 `0.3.0`、`esp_codec_dev ~1.6.2`、
ESP-SR `2.4.7` 和 `espressif/bme690 1.1.0`。BME690/BMI270 适配不手改
`managed_components/`，由组件管理器下载官方来源。

```sh
# Bash：替换 SDK 安装路径
source /path/to/esp-idf/export.sh
idf.py --version
```

```powershell
# Windows PowerShell：按实际安装路径修改
. C:\esp\v6.1\esp-idf\export.ps1
idf.py --version
```

## 从干净检出构建

```sh
python -m pip install Pillow
python custom_assets/build_eaf_assets.py --pack-only
python scripts/build.py espressif/esp-sensairshuttle --name esp-sensairshuttle --language zh-CN
```

顺序不能反：该发布变体在构建时引用发布素材包，而 `.bin` 是生成文件，不随
仓库提交。`--pack-only` 使用仓库内的 EAF、字体、唤醒模型和两张背景 PNG，
无需下载个人目录中的原动画压缩包。

GitHub Actions 同样先安装主机测试所需 Pillow，并仅在此 C5 板型的构建任务中
运行素材打包步骤，随后调用统一构建脚本；其他板型不执行看山素材转换。

若修改 GIF，再执行不带 `--pack-only` 的转换命令。六组源 GIF 位于
`custom_assets/kanshan_motion_v1/staging/pet_*.gif`；EAF 位于
`custom_assets/kanshan_eaf_v1/staging/`。字体和 `srmodels.bin` 是必需输入，
虽然扩展名为 `.bin`，仍已作为源资源放行 Git 忽略规则。

场景源文件为 `custom_assets/scenes/igloo_day_284x240.png` 与对应 night PNG。
JPEG 备用图不进入包；脚本拒绝错误尺寸、透明背景和损坏资源。运行时使用
RGB565 CBin，不加载 PNG/JPEG 解码画布。

## 子板配置

默认发布变体开启 `CONFIG_USE_BME690_NOSE=y`。换回 BMI270/BMM350 时，在
板级 `config.json` 的变体配置中改为 `CONFIG_USE_BME690_NOSE=n`，保留
`CONFIG_USE_KANSHAN_PET=y` 后重新执行规范构建。不要直接修改生成的 `sdkconfig`。

`scripts/build.py` 会改变本地配置与构建目录；每次先确认目标为 ESP32-C5，
不能把上一次其他芯片的构建文件直接烧入本板。

## 分区与地址

分区源文件：[partitions/v2/16m.csv](../../partitions/v2/16m.csv)。

| 区域 | 地址 | 大小 / 作用 |
| --- | --- | --- |
| Bootloader | `0x2000` | 以当前构建产物大小为准 |
| 分区表 | `0x8000` | 分区表扇区 |
| NVS | `0x9000` | `0x4000`，保存配置，不在常规烧录列表 |
| OTA data | `0xd000` | `0x2000`，启动槽位信息 |
| PHY init | `0xf000` | `0x1000` |
| ota_0 | `0x20000` | `0x3f0000` |
| ota_1 | `0x410000` | `0x3f0000` |
| assets | `0x800000` | `0x800000`，8 MiB |

```sh
# 将 COMx 换成实际串口；Linux/macOS 换成对应设备路径
idf.py -p COMx flash
idf.py -p COMx monitor
```

发布配置包含应用和配套素材；新场景需要二者匹配。实际烧录参数以构建生成的
`build/flash_args` / `flasher_args.json` 为准。全量 Flash 备份含设备配置，仅保留本地。
先核对设备型号、分区与 NVS 位置；本项目调试未采用整片擦除。

仅更新应用时，还需确认当前启动槽位；不能不检查 OTA data 就假设正在运行 ota_0。
本次调试确认 ota_0 后只写应用，并保留刚配置的热点设置。

## 校验与测试

```sh
python -m unittest discover -s scripts/tests -v
```

完整官方解码器验证工具为 `custom_assets/verify_eaf_decoder.py`，依赖已下载的
官方 C 解码器和支持相应目标的本机 LLVM；它不是运行固件所需依赖。

格式检查只针对修改的 C/C++ 文件。改 UI 时同时检查关闭桌宠、OLED/NoDisplay
路径；改音频时实测录音、播放、打断、重连，不能只看构建通过。

## 常见现象

| 现象 | 优先检查 |
| --- | --- |
| 构建前提示找不到自定义 assets | 先运行 `--pack-only`，确认两张 PNG、EAF、字体/模型都存在 |
| COM 口拒绝访问 | 关闭占用该端口的串口监视器；不要求擦除设备 |
| USB 复位后端口暂时消失 | 重新枚举同一设备并连接，调试日志应实时保存 |
| 只显示 128px 黑白照 | 配网/启动回退可能正常；联网后确认 `Assets::Apply()` 与 EAF 资源加载 |
| `missing/invalid ... pet_idle` | 核对素材包与加载阶段；配网音频测试可能先于场景/动画加载 |
| `hardware_ok=false` | 查自检、数据新鲜度、热板有效位；不能仅看 I²C ACK |
| 温度或电阻变化大 | 区分自检预热、板上自热、环境变化和无效首帧，不直接换算气味类别 |
