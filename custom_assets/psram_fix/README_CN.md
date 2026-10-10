# SensairShuttle PSRAM / 刘看山显示修复

## 已确认的原因

- 用户原理图第 2 页 U1：ESP32-C5-WROOM-1-N16R8；开发板自带 8 MB PSRAM。
- 修复前实际配置：`# CONFIG_SPIRAM is not set`。Flash 空间不能替代 GIF 解码需要的 RAM。
- 当前 GIF 解码器为像素缓冲分配 `5 * width * height` 字节，另有结构体及 LZW 字典。
- 192×192 动画仅像素缓冲就需要 184320 字节，而报错时空闲 SRAM 只有 57680 字节。
- 48×48 低内存包可显示，用户已确认。但启动实测还发生 TLS/音频分配失败，最低 SRAM 仅 2300 字节，因此不能作为最终修复。

## 配置修复

通过 `custom_assets/enable_board_psram.py` 调用 ESP-IDF 的配置服务器设置，不手工编辑生成配置：

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=3072
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_MEMTEST=y
```

其余依赖变化记录在 `configuration_changes.json`。保持原有普通 LVGL 界面、引脚、分区、网络配置与 ADC 临时禁用状态，不启用默认变体中的 Emote。

## 备份

- `config_before.txt`：修复前实际配置。
- `application_before.bin`：修复前本地应用，已比对与设备 ota_0 的前缀完全一致。
- `device_ota0_before.bin`：设备原 ota_0 整分区备份，地址 0x20000，长度 0x3f0000。
- `bootloader_before.bin`：原本地引导程序。
- `../kanshan_idle_v1/device_assets_before.bin`：修改动画前板上 assets 全分区备份，地址 0x800000，长度 0x800000。

仅烧录应用和所需资源，不覆盖 NVS、otadata 或分区表。进行恢复前应先核对串口设备和分区表。

## 验证记录

- 低内存 GIF：100 帧逐帧对照、5 秒循环、透明背景和资源完整性校验通过。
- 主机测试：81 项中 76 项通过，5 项在 Windows 删除当前临时工作目录时发生 WinError 32/5；见 `tests.log`，未改动测试绕过错误。
- PSRAM 固件：ESP-IDF v6.1 构建成功，应用 2410704 字节，分区剩余 42%。
- 应用已烧录到 COM10 / ESP32-C5 / ota_0 0x20000，esptool 写入哈希校验通过。
- 设备日志确认 `Found 8MB PSRAM device`、40 MHz、`SPI SRAM memory test OK`，8192K PSRAM 已加入堆。
- PSRAM + 48×48 资源观察 50 秒：进入待机与监听，未再出现 GIF/TLS/音频分配失败。
- 随后恢复 192×192 原资源到 0x800000，写入校验通过。见 `runtime_psram_192.log`：55 秒观察中多次 idle/connecting/listening 切换，无 GIF 加载失败、TLS/音频分配错误、崩溃或二次重启。
- 大动画观察期末：空闲内部 SRAM 70995 字节，历史最低 22892 字节。只代表本次短时测试，不代表长时间压力测试完成。
- 用户已确认 48×48 可见；恢复 192×192 后的实际屏幕观感仍需用户确认。

最终应用：`xiaozhi_psram.bin`，SHA256 `92a3e781a814d2c16bb6638aa21ac3471dc32b697da70ab646ea57b04ea6b19d`。

最终资源：`../kanshan_idle_v1/kanshan_idle_assets.bin`，SHA256 `7651fca70a23416026e49d4421ab000d26aeaa06c55886e7de6147dd66c56ab9`。

ESP-IDF 构建时出现未设置 `ESP_ROM_ELF_DIR` 的调试器初始化文件警告，不影响最终应用生成与本次烧录。

芯片 rev v1.0 启动提示 PSRAM 加密限制。当前仍保留 `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`，没有启用 TLS 外部内存分配，也没有修改 Flash 加密配置。

## 注意

ADC 麦克风此前已临时禁用，本次没有恢复。进入 listening 只表示状态切换，不证明真实采音可用。

当前默认完整烧录会写回 `build/generated_assets.bin`，覆盖刘看山资源；应在需要时单独重新写入所选资源包。
