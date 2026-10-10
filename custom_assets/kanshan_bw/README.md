# Black-and-white Liu Kanshan idle asset

Generated with the built-in imagegen tool from the user's
`Downloads/看山三视图/liukanshan-avatar.jpg`. The source reference is not modified.

Prompt: Convert the supplied Liu Kanshan avatar to a black-and-white illustration
on a truly transparent background. Preserve the silhouette, ears, face, black oval
muzzle, waving arm, body proportions and painting-palette accessory. Remove the
blue rounded backdrop and circuit traces; no text, new objects or watermark.

`liukanshan_bw_source.png` is the generated source. The current C5 build uses
`../../main/pet/kanshan_bw_128_image.cc`, a 128x128 Flash-resident ARGB8888 descriptor
(65,536 bytes), for boot/provisioning and animation failure fallback.
`liukanshan_bw_128.png` is its preview. The 64px preview/descriptor belong to the
older stickman renderer and are not compiled into the current C5 variant.
Runtime rendering does not read these PNG files or require a PNG decoder.

Regenerate the encoded resource from the project directory:

```powershell
python custom_assets/build_bw_avatar.py --source custom_assets/kanshan_bw/liukanshan_bw_source.png --output main/pet/kanshan_bw_128_image.cc --side 128 --symbol kKanshanBw128Image --preview custom_assets/kanshan_bw/liukanshan_bw_128.png
```
