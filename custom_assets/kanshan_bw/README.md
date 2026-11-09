# 128px 开机与配网黑白照

`liukanshan_bw_source.png` 是由用户参考图生成并确认的透明黑白源图，
`liukanshan_bw_128.png` 是当前预览。固件使用
`main/pet/kanshan_bw_128_image.cc` 内的 128×128 ARGB8888 资源，
共 65536 B 像素，运行时不解码 PNG。旧 64px 资源和火柴人渲染器已移除。

在工程根目录重新编码：

```sh
python custom_assets/build_bw_avatar.py --source custom_assets/kanshan_bw/liukanshan_bw_source.png --output main/pet/kanshan_bw_128_image.cc --preview custom_assets/kanshan_bw/liukanshan_bw_128.png
```

脚本默认并限制为当前使用的 128px 规格；仅缩放/编码已确认源图，不重新生成角色。
[素材总览](../README.md) · [桌宠与界面](../../docs/c5/desktop-pet.md)
