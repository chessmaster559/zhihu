# 看山六组源 GIF

这里只保留当前 EAF 流程需要的 `staging/pet_*.gif` 六个源文件：待机、打招呼、
晃悠、电脑、瞌睡和玩耍。它们保持 128×128，用于重新转换和官方 EAF 解码验证。
当前固件不直接播放此目录的 GIF，也不使用旧 GIF 资源包。

```sh
python custom_assets/build_eaf_assets.py
python custom_assets/verify_eaf_decoder.py
```

若只重建发布包，使用 `build_eaf_assets.py --pack-only`。
[当前素材说明](../README.md) · [桌宠行为](../../docs/c5/desktop-pet.md)
