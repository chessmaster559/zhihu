# XiaoZhi 来源与本分支范围

本仓库是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的 C5 定制派生
分支，面向刘看山桌宠，不是 XiaoZhi 官方发布。保留原工程的 Git 历史、公共
框架、设备端 MCP、音频和网络实现，以便追溯来源及合并上游修复。

| 项目 | 说明 |
| --- | --- |
| 上游 | `https://github.com/78/xiaozhi-esp32`，本地 remote 名称 `upstream` |
| 本分支 | `https://github.com/chessmaster559/zhihu`，本地 remote 名称 `origin` |
| 主要板卡 | ESP-SensairShuttle / ESP32-C5，16 MB Flash、8 MB PSRAM |
| SDK | 优先 ESP-IDF 6.1，最低 6.0.1，不支持 5.x |
| 本分支 CI | 仅构建 `espressif/esp-sensairshuttle` / `esp-sensairshuttle` |
| 许可证 | 保留原 [MIT LICENSE](LICENSE) 和版权署名；第三方依赖及厂商资料遵循各自许可 |

定制内容：128px EAF 桌宠、自主动作、字幕与漫画气泡、触摸爱心、昼夜场景、
ADC/PDM 板级适配和 BME690 鼻子查询。当前状态与未完成能力见
[技术文档](docs/c5/README.md)和[验证记录](docs/c5/validation-and-limits.md)。

## 清理原则

当前发布流程使用的 EAF、六组源 GIF、字体、唤醒模型、128px 黑白照及场景
PNG 均保留。旧 idle/lowmem 素材、旧 GIF 发布工具、64px/火柴人实现、重复
预览、宣传图片和重复调试说明已退出当前文件树，历史内容可从 Git 历史追溯。

其他板卡源码属于保留的上游框架。本分支未将其列入 CI，也不宣称它们经过
本分支回归。保留这些实现避免破坏上游 Kconfig/CMake/构建脚本的耦合关系，
不代表它们是当前 C5 的必需运行资源。

## 维护与合并

获取上游变化后，在独立工作分支审查合并，重点检查 SDK、板卡身份、引脚、
音频队列、协议语义、资源格式和分区；不直接覆盖本分支的板级配置。

```sh
git remote -v
# 首次克隆且尚未设置 upstream 时执行；已有该 remote 则跳过
git remote add upstream https://github.com/78/xiaozhi-esp32
git fetch upstream
git switch -c maintenance/upstream-update
git merge upstream/main
```

合并前先提交或保存本地工作，解决冲突后按 [AGENTS.md](AGENTS.md) 验证。
需要在硬件上复测的结论应单独记录，不因成功合并或编译就标为已验证。
