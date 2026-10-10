# 刘看山桌宠动画接入（2026-10-05）

选定板型：ESP32-C5 / ESP-SensairShuttle，显示 284×240、PSRAM 8MiB。

原素材位于 `C:\Users\T2071\Downloads\看山三视图\刘看山动态`。六个原 GIF 未修改；转换文件为 128×128、63 个可见色加透明色，保留总时长。相邻完全相同的帧会合并，50ms 动作帧保留。浅/深背景逐帧校验及实际 C 解码验证均通过。

## 屏幕行为（独立动作状态机）

- 开机初始化、配网、激活和升级：Flash 中的 128×128 黑白看山图，状态和提示文字保留。资源应用完成且离开这些状态后，先播放一次 `pet_greeting.gif`（4 秒），再进入待机；每次开机只自动问候一次。
- 待机：`pet_idle.gif` 循环。进入待机满 10 秒后，晃悠 `pet_sway.gif`（3 秒）→玩耍 `pet_play.gif`（4 秒）交替播放；正常切换由 GIF 完成事件驱动，不按名义时长截断慢帧动画。
- 没有有效用户输入满 30 秒：切换至 `pet_sleep.gif` 循环。初始化完成开始正常桌宠时建立第一次计时；动画完成不会刷新静音计时。瞌睡仅为显示动作，不关闭麦克风、唤醒或网络。
- 用户识别文本“帮我”或以“帮我”开头：播放电脑 `pet_thinking.gif` 一次（6 秒），随后待机；“你好”：打招呼一次后待机；“睡觉”或“说出睡觉”：立即瞌睡，直到新的有效输入唤醒。
- 匹配时去除空格、中英文标点；“不要睡觉”“他跟我说你好”等不按子串触发。连续说两次同一口令会重播；空白/纯标点不是有效输入。只处理 `stt` 用户文本，助手 `tts` 文本和服务器 `llm` 表情不触发这些动作。
- 连接、聆听、播报和通知状态不覆盖当前动作，也不刷新计时。字幕、状态栏和声音照常更新；设备播报期间，10/30 秒计时继续进行。告警文本/提示音保留，只有配网、激活、升级等初始化状态切换为黑白图。
- C5 当前 LiteAudioEngine 只有 VAD 回调注册，没有实际 VAD 判断，因此计时依据有效 STT/唤醒事件；保留原有触摸主动交互唤醒及重置计时。具有实际 VAD 的音频引擎会在持续用户人声时刷新。口令通过现有在线识别通道，在唤醒后的语音会话中生效；本次未加入离线关键词识别模型。
- 有效触摸仍打招呼，聊天释放操作保留；切入语音不会打断问候。倾斜/翻转继续复用角色旋转，BMI270 是否应答须独立验收。
- 图片预览期间暂停动画，结束后恢复；重复预览保留恢复标志。重复应用相同动作与序号不重启 GIF；新口令使用新序号强制重播。过期播放器的 Started/Finished/Failed 事件按序号丢弃。
- GIF 完成回调仅通过 `Schedule()` 提交主任务事件，不能在自己的计时器回调中释放播放器。缺素材/解码失败回退静态图，按素材时长退出短动作；已启动的单次动画有 20 秒挂起保护。10/30 秒由现有 1 秒时钟检查，正常调度时计时切换误差小于约 1 秒，不新建任务。

重点代码注释：`PET-SILENCE`、`PET-AUDIO`、`PET-COMMAND`、`PET-LIFETIME`、`PET-CALLBACK`、`PET-LEGACY`，分别标记计时、音频独立、口令、防旧事件、回调生命周期和兼容显示入口。

## 资源预算与失败处理

| 项目 | 预算或验证结果 |
| --- | --- |
| 整个资源包 | 3,154,877 字节；含原有 24 个非索引文件，字体/模型/原表情字节保持一致 |
| 8MiB 素材分区剩余 | 5,233,731 字节，约 62% |
| 一个 128px GIF 的画布＋索引 | 81,920 字节；压缩文件映射自 Flash，不展开所有帧 |
| LZW 表上限 | 约 24KiB；重分配时可能短暂同时保留旧表 |
| 主机实际 C 解码分配峰值 | 120,480 字节（约 118KiB），包含重分配重叠；不包含 LVGL 绘制缓存/栈/其他系统资源 |
| 同时存活的 GIF 解码器 | 1；先撤销 source/cache、释放旧播放器，再创建新播放器 |
| 开机/配网静态图 | 65,536 字节 ARGB8888 存于 Flash，无 GIF 画布/PNG 解码 |
| 应用分区余量 | 本次构建约 37%，详见 build 日志 |

SensairShuttle 的 GIF 画布、索引和 LZW 表显式用 `MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT` 分配；失败不回退内部 SRAM。播放器对象和少量 UI/timer 对象仍有常规堆开销，LCD 的内部 DMA 缓冲仍沿用原实现，因此不能声称内部 SRAM 完全不增加。

首次 LZW 表分配空指针漏洞已修复。初始解码/资源缺失直接回退静态图；后续帧失败由独立 LVGL 1s 健康定时器释放失败解码器再回退，不在 GIF 自己的回调内销毁自己。GUI 正常调度时最多约 1s 检查一次。健康定时器分配失败会打印警告。

## 重建与验证

```powershell
py -3.14 custom_assets/build_motion_assets.py --source-dir 'C:\Users\T2071\Downloads\看山三视图\刘看山动态'
py -3.14 custom_assets/build_bw_avatar.py --source custom_assets/kanshan_bw/liukanshan_bw_source.png --output main/pet/kanshan_bw_128_image.cc --side 128 --symbol kKanshanBw128Image --preview custom_assets/kanshan_bw/liukanshan_bw_128.png
py -3.14 custom_assets/verify_motion_decoder.py
```

主机测试：87 项通过，无跳过。真实 PetController/策略测试覆盖 10/30 秒边界、动画真实结束、语音/通知不中断、三个中文口令及标点/否定句/超长文本、同动作重播、过期事件、资源延迟与缺失、挂起保护、毫秒回绕、配网图片及其他板型兼容；控制器主机测试用短字符串 shim，不验证标准库分配。真实 C GIF 解码器验证 6 个 GIF、532 帧的浅/深背景像素、时长、循环边界、单次播放及画布/LZW 分配失败清理。

ESP-IDF v6.1 `build merge-bin` 和镜像 checksum/hash 校验通过。当前应用 2,608,464B（0x27cd50），相较初版动画固件增加 3,264B，分区仍余约 37%；ELF SHA 前缀 `c3f78358`。素材包未改变 SHA/大小，本次沿用此前实际 C 解码验证。配置与板型默认资源路径均指向本目录资源包。

本次未烧录，不能把主机时间或预算当作实机帧率/堆余量。设备首次使用这套动画时需同时更新应用和资源包。推荐在 ESP-IDF 环境执行 `idf.py -p COM10 flash`，或在 build 目录用 esptool `write-flash "@flash_args"` 按分区烧录；这会保留 NVS。`build/merged-binary.bin` 包含固件与素材，整体偏移为 `0x0`，但 raw 合并镜像的填充区会覆盖 NVS，使用它会清除配网信息。已经烧录同一素材包的设备可仅更新应用。

实机观察：`Kanshan: asset=boot_bw128 mode=flash128`、`pet_idle mode=gif128`、`Animation stats`；连续对话期间记录 SRAM/DMA 最大连续块、PSRAM、`main`/LVGL/audio 任务栈快照。确认透明背景无残影、配网提示可读、触摸不误触、动画没有打断音频、长期切换无持续内存下降。历史 SRAM minimum≈14KB，因此内部峰值仍是主要验收项。

预览：`preview.png`；素材与校验元数据：`manifest.json`。当前验证日志：`build/kanshan_commands_tests_final.log`、`build/kanshan_commands_build_final.log`、`build/kanshan_commands_image_info.log`；合并镜像 5 个分区逐字节校验、文件 SHA 与大小：`build/kanshan_commands_artifacts.json`。此前素材解码验证保留于 `build/kanshan_decoder_verification.json`。

实机动作验收顺序：正常初始化后问候→待机 10 秒→晃悠/玩耍；无用户输入至第 30 秒进入瞌睡；唤醒后依次说“帮我查天气”“你好”“睡觉”，检查对应动画及自然结束。长播报期间动作不被 speaking/listening 或服务器表情重置；字幕继续更新；说“不要睡觉”不误触发瞌睡。连续切换期间观察 GIF 帧率、SRAM/DMA/PSRAM 与任务栈。
