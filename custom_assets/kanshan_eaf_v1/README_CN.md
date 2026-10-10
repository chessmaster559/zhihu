# 看山 EAF 素材

使用乐鑫官方 `espressif/esp_lv_eaf_player` 0.3.0，在原有 LVGL 界面中播放。
原始六组 128px GIF 保留在 `../kanshan_motion_v1/staging/pet_*.gif`。
当前生成流程直接读取这些源文件，不依赖旧的 48px/192px 素材包或设备备份。

在工程根目录运行：

```sh
python custom_assets/build_eaf_assets.py
python custom_assets/verify_eaf_decoder.py
```

仅重建发布包（无需原 GIF 归档）：

```sh
python custom_assets/build_eaf_assets.py --pack-only
```

从新检出的工程构建固件前先运行上述打包命令。生成的发布 `.bin` 不提交；
`staging/font_noto_sans_common_16_4.bin` 和 `staging/srmodels.bin` 是必要的
输入资源，已在 `.gitignore` 中单独放行，提交时需将它们一起加入。
它们分别提供中文字体和唤醒模型，不是可删除的临时构建产物。

转换脚本需要 Pillow；验证脚本使用 Windows LLVM 和下载到
`managed_components` 的官方 C 解码器，未修改组件源码。
六个动作保持 128×128、原始透明度和播放时长，以 50 ms 索引播放；
较长的原始帧重复索引，帧数据去重。运行时输出使用 RGB565+A8。

当前包为 5,108,821 字节，8 MiB assets 分区剩余 3,279,787 字节。
仅含六组 EAF、字体、唤醒模型和索引，共 9 个文件；索引不再引用已删除的
21 个通用 PNG 表情。打包只接收这些资源，即使 staging 残留 PNG 也不会带入。
单个解码帧缓冲 49,152 字节，组件优先使用 PSRAM；主机实测官方解码器
辅助堆内存峰值最高 5,072 字节，不含 LVGL 对象、任务栈及显示缓冲。
该数值来自 64 位主机，实际 C5 内存用量仍需上板确认。

`decoder_verification.json` 记录全部 540 帧的 RGB565 像素、alpha、时长和
内存不足时的资源清理验证。`manifest.json` 记录包大小和 SHA-256。
校验通过不代表屏幕帧率或音频噪声已在硬件上验证。

激活 ESP-IDF 6.1 后按板卡发布配置构建：

```sh
python scripts/build.py espressif/esp-sensairshuttle --name esp-sensairshuttle --language zh-CN
idf.py -p COMx flash
```

将 `COMx` 替换为实际串口。发布配置包含固件和此素材包，后者位于
`0x800000`；仅更新应用固件不会更新 Flash 中的旧 GIF 包。
`CONFIG_USE_EAF_ANIMATIONS` 启用官方播放器，`CONFIG_USE_KANSHAN_PET`
启用独立桌宠动作控制器，`CONFIG_USE_DEFAULT_MESSAGE_STYLE` 保留 LVGL 界面，
EAF 的软件 JPEG 解码关闭。六个 `pet_*` 动作全部为 128×128；旧的通用表情
条目已从本地新包移除，启动/配网和解码失败时显示固件内的 128×128 黑白照。

2026-10-10 的清理仅修改本地素材生成流程和发布包，尚未重新烧录该精简包。
已烧录固件使用同样的六组 EAF，其动画、字体和模型与新包逐字节一致。
