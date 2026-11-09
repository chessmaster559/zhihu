# 当前 C5 素材与工具

发布配置使用 `kanshan_eaf_v1/kanshan_eaf_assets.bin`，在工程根目录执行：

```sh
python custom_assets/build_eaf_assets.py --pack-only
```

| 路径 | 当前用途 |
| --- | --- |
| `kanshan_eaf_v1/staging/` | 六组 128px EAF、索引、中文字体和唤醒模型 |
| `kanshan_motion_v1/staging/pet_*.gif` | 六组原始动作，供重新转换和真实解码器验证 |
| `kanshan_bw/liukanshan_bw_source.png` | 128px 开机/配网照的源图 |
| `kanshan_bw/liukanshan_bw_128.png` | 静态照预览；运行时使用固件内的编码资源 |
| `scenes/igloo_*_284x240.png` | 昼夜背景源图，打包时转换为 RGB565 CBin |
| `build_eaf_assets.py` / `asset_pack.py` | 当前素材转换、打包及校验解包 |
| `build_bw_avatar.py` | 生成当前 128px Flash 静态图，默认即为 128px |
| `verify_eaf_decoder.py` / `eaf_decoder_test_support.py` | 官方 EAF 解码器逐帧、alpha、时长和低内存验证 |
| `capture_runtime.py` | C5 串口诊断采集，可指定串口和输出路径 |

字体与 `srmodels.bin` 是必需源资源，已单独放行 Git 忽略；其他生成 `.bin`
可重建。每张背景 CBin 的像素直接从映射 Flash 读取，不在运行时解码 PNG。
旧 idle/lowmem 包、PSRAM 一次性修复器、旧 GIF 发布脚本、64px 头像及重复
预览已清理，所需共用函数已迁移为独立模块。旧方案可从 Git 历史追溯。

详见 [构建与素材流程](../docs/c5/build-and-flash.md)、
[分支说明](../UPSTREAM.md)和[验证记录](../docs/c5/validation-and-limits.md)。
