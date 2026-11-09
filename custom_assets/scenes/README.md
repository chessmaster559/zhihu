# 冰屋场景源图

保留 `igloo_day_284x240.png`、`igloo_night_284x240.png`，它们是当前 C5
素材打包的必要输入。不参与打包的 JPEG 备用副本已清理。

在工程根目录执行 `python custom_assets/build_eaf_assets.py --pack-only`，
会将不透明的 284×240 PNG 转成 C5/LVGL9 的 RGB565 CBin，并加入原来的
8 MiB assets 包；不改变六组桌宠动画、字体和唤醒模型。
原 PNG 不修改，生成的 `.bin` 不需要提交，可以重新生成。

白天醒着显示日景；睡眠或本机时钟处于 19:00–次日 08:00 时显示夜景。
时间未同步时只依据睡眠状态。背景切换不强制桌宠睡觉、不重启动画。
完整说明及离屏预览见 [桌宠与场景](../../docs/c5/desktop-pet.md)。
