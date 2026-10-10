# C5 看山素材清理说明

当前板卡发布配置只使用 `kanshan_eaf_v1/kanshan_eaf_assets.bin`。
该包可以用 `python custom_assets/build_eaf_assets.py --pack-only` 重建。

需要保留：

- `kanshan_eaf_v1/staging/`：六组 128×128 EAF、索引、中文字体和唤醒模型。
- `kanshan_motion_v1/staging/pet_*.gif`：重新转换和逐帧解码验证的原始动作。
- `kanshan_bw/liukanshan_bw_source.png`：重新生成开机黑白照的源图。
  当前运行使用 `main/pet/kanshan_bw_128_image.cc`，不在运行时读取 PNG。
- 素材生成、验证脚本及 `main/pet/` 中当前构建引用的源码。

旧 staging 目录中的通用 32/48px PNG 已无运行引用，可以清理。128px 黑白照的
预览 PNG 不影响运行，但可帮助核对重新生成的资源。64px 头像、旧火柴人渲染器
不在当前 C5 构建中；`kanshan_idle_v1`、`kanshan_idle_lowmem_v2`、`psram_fix`
主要是历史素材、诊断日志及回滚备份，删除会失去历史记录或回滚能力。
当前 C5 打包不再依赖这些历史目录；旧的 `build_idle_assets.py`、
`build_motion_assets.py` 仍是历史生成流程，运行它们需要相应旧输入。

`.bin` 扩展名不代表临时文件：当前 staging 的字体和 `srmodels.bin` 是
不可缺少的源资源，已从通用忽略规则中单独放行。它们需要一同提交，
否则新检出的工程不能重建完整素材包。设备 NVS 备份含配置数据，应留在本地。
