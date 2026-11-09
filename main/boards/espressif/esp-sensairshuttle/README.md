# ESP-SensairShuttle

本仓库的 C5 刘看山定制版本请先阅读
[完整技术文档](../../../../docs/c5/README.md)、
[硬件与子板选择](../../../../docs/c5/hardware.md)及
[厂商原理图/PCB/数据手册](../../../../docs/hardware/README.md)。

## 简介

<div align="center">
    <a href="https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32c5/esp-sensairshuttle/index.html">
        <b> 开发版文档 </b>
    </a>
    |
    <a href="#传感器--shuttleboard-子板支持">
        <b> 传感器 & <i>ShuttleBoard</i> 文档 </b>
    </a>
</div>

ESP-SensairShuttle 是乐鑫携手 Bosch Sensortec 面向**动作感知**与**大模型人机交互**场景联合推出的开发板。

ESP-SensairShuttle 主控采用乐鑫 ESP32-C5-WROOM-1-N16R8 模组，具有 2.4 & 5 GHz 双频 Wi-Fi 6 (802.11ax)、Bluetooth® 5 (LE)、Zigbee 及 Thread (802.15.4) 无线通信能力。

## 传感器 & _ShuttleBoard_ 子板支持

即将推出，敬请期待。

## 配置、编译命令

由于 ESP-SensairShuttle 需要配置较多的 sdkconfig 选项，推荐使用编译脚本编译。

**编译**

```bash
python ./scripts/build.py espressif/esp-sensairshuttle
```

如需手动编译，请参考 `main/boards/espressif/esp-sensairshuttle/config.json` 修改 menuconfig 对应选项。

## 看山动画与 PDM 功放

当前发布配置使用 LVGL 界面和乐鑫官方 `esp_lv_eaf_player` 0.3.0
（`CONFIG_USE_EAF_ANIMATIONS=y`），素材来自
`custom_assets/kanshan_eaf_v1/kanshan_eaf_assets.bin`。原 GIF 保留在
`custom_assets/kanshan_motion_v1/`；安装 Pillow 后，可运行
`python custom_assets/build_eaf_assets.py` 重新转换六个 128×128 动作。
每帧 50 ms，较长的 GIF 帧通过重复索引保留原时长和透明度。
EAF 解码帧缓冲为 RGB565+A8，共 49,152 字节，组件优先使用 PSRAM。
含昼夜背景的素材包约 5.38 MB，使用既有的 8 MiB assets 分区。

PDM 沿用原引脚：GPIO7 为正相、GPIO8 为同一 PDM 信号的反相，GPIO1
控制 NS4150B 功放。输出前先保持功放关闭，填充零 PCM、启动 PDM，再打开
功放并对首个 10 ms 音频淡入。输出队列排空后等待 300 ms，由输出任务关闭
功放、停止 PDM，再将 GPIO7/8 都切回低电平 GPIO；定时器不再关闭输出。
原实现已经通过 IO 矩阵反相 GPIO8；当前主要修正引脚配置和启停顺序，
减少空闲时的开关活动。IO 矩阵不能替代电源滤波，也不能保证消除 Wi-Fi
供电耦合产生的噪声。

PDM 时钟使用官方 `I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG`：24 kHz PCM 时
`fp=960/fs=240/bclk_div=13`，名义 PDM 位流速率 6.144 MHz；启动日志记录
这些配置。300 ms 关断滞回用于减少短暂停顿时的功放反复启停，不在音频
队列中增加缓冲或延迟。它可能延长播报结束后的底噪窗口，不能保证消除底噪。
原分贝音量曲线和 HP DIV2 保持不变；本轮时钟修改的实际噪声仍需上板确认。

`CONFIG_USE_KANSHAN_PET=y` 将桌宠控制器、应用事件和 EAF 播放完成回调接通。
开机和配网使用 Flash 中的 128×128 黑白看山照；正常启动后打招呼，随后待机，
待机 10 秒进入晃悠/玩耍，30 秒没有用户语音进入瞌睡。“帮我”进入电脑动作，
“你好”打招呼，“睡觉”进入瞌睡。播报和服务器表情不会替换或重启动作。
播放器只选择 `pet_*` 的六组 128×128 EAF，失败时回退黑白照。

识别文字显示在底部；播报文字使用头顶圆角漫画气泡，长句滚动显示。
睡眠时叠加上浮 Zzz，有效触摸时叠加渐隐爱心；叠加层复用小控件，不新增
GIF 或解码画布，也不会重启桌宠动作。布局预览与验证见
[`docs/c5/desktop-pet.md`](../../../../docs/c5/desktop-pet.md)。

`custom_assets/scenes/` 的两张 PNG 在打包时转换成可直接从 Flash 读取的
RGB565 场景。看山睡眠，或本机时钟处于 19:00–次日 08:00 时显示夜景；
08:00–19:00 且醒着显示日景。时钟未同步时只依据睡眠状态，切换不重启动作，
也不写 NVS。新背景需要同时更新固件和素材包，预览及验证见
[`docs/c5/desktop-pet.md`](../../../../docs/c5/desktop-pet.md)。

冰屋中看山的脚对齐左侧地垫，日常状态文字隐藏；等待说话显示淡绿点，控件
支持 VAD 驱动的加深提示，C5 本地 VAD 输入尚待实现。
等待回复显示小灯泡。夜景对 EAF 与回退照施加约 12.5% 深蓝染色，日景恢复
原色。当前预览、布局依据及资源说明见
[`docs/c5/desktop-pet.md`](../../../../docs/c5/desktop-pet.md)。

麦克风已启用官方 `esp_codec_dev` ADC DMA 接口：ADC1_CH5、16 kHz、
640 字节 DMA 帧。输入任务使用 6144 字节栈，容纳官方 ADC 解析临时数组；
PCM 去除模拟偏置，连续读取失败后退避重启，每 5 秒输出采样统计。
音量保留原 NVS 值，使用原来的 -50..0 dB 曲线及有效 HP DIV2 设置，
0 为静音。BMI270 模式沿用 SDA2/SCL3、CS10 高、SDO9 低；鼻子模式不启动
BMI270 任务。原配两类子板互换，选择规则见当前技术文档。

`CONFIG_USE_BME690_NOSE=y` 启用原配 BME690 子板的官方自检和每 3 秒一次的
温湿压/原始气体电阻采集，沿用 SDA2/SCL3、CS10 高、SDO9 低，地址 0x76。
“闻一闻”入口为设备 MCP 工具 `self.pet.nose.get_status`；该版本尚无 BSEC IAQ
和气味分类模型，不能把原始电阻当成空气质量或具体气味。接入、状态判据及
验证说明见 [`docs/c5/nose-and-mcp.md`](../../../../docs/c5/nose-and-mcp.md)。

编译和主机测试不能代替硬件验证；实际语音识别、动作播放、响度及 Wi-Fi
收发噪声仍需结合本次串口日志和实机观察确认。
烧录时需同时更新固件和新素材包；上述发布配置会将素材加入烧录参数。

**烧录**

```bash
idf.py flash
```
