<p align="center"><img src="assets/app.png" width="96" alt="Codex Quota Bar" /></p>

# Codex Quota Bar

在 Windows 任务栏显示 Codex 额度和 DeepSeek 账户余额，支持多屏、外观设置、悬浮详情和自动刷新。

**最新发布：[0.3.2](https://github.com/SaturdayF/codex-quota-bar/releases/tag/v0.3.2)** · Windows 10/11 x64 · C++ 原生程序

**稳定版：[0.3.1](https://github.com/SaturdayF/codex-quota-bar/releases/tag/v0.3.1)**

[安装包](https://github.com/SaturdayF/codex-quota-bar/releases/download/v0.3.2/CodexQuotaBar-v0.3.2-Setup.exe) · [便携包](https://github.com/SaturdayF/codex-quota-bar/releases/download/v0.3.2/CodexQuotaBar-v0.3.2-Portable-win-x64.zip) · [版本记录](CHANGELOG.md)

功能和设置入口见 [使用指南](native/USER_GUIDE.md)。

## 功能

| 功能 | 说明 |
| --- | --- |
| Codex 额度 | 显示 5 小时和周额度，可分别开关；支持按套餐选择默认显示项 |
| DeepSeek 余额 | 显示账户余额、北京时间当日消费及按模型汇总的费用、请求数和 Token |
| 自动跟随 | Codex 组件随 ChatGPT / Codex 桌面进程显示；DeepSeek 组件随 Harness 桌面进程显示，也可独立常驻 |
| 多屏与布局 | 选择主屏、指定屏幕或全部屏幕；自动避让可识别的任务栏图标，支持拖动和组件交换 |
| 外观 | 七种样式、两种额度独立配色、60%–140% 缩放、百分号、余量图形及 H/W 标记 |
| 动画 | 圆环、边框和进度线填充，数字递增，以及拖动落位过渡 |
| 悬浮与设置 | 悬浮查看详情，右键打开设置，修改后可预览并保存 |
| 刷新 | Codex 可选 15 秒至 5 分钟，DeepSeek 可选 1 / 2 / 5 分钟；支持手动刷新和失败重试 |
| 安装与卸载 | 安装包支持自定义目录；安装版与便携版均提供卸载按钮 |

### 外观与额度详情

![七种样式](docs/native-styles.png)

![动画演示](docs/animation-preview.gif)

![外观设置](docs/native-settings-preview.png)

<img src="docs/native-details-preview.png" width="372" alt="Codex 额度详情" />

### DeepSeekHarness

![余额与消费](docs/deepseek-widget-preview.png)

<img src="docs/deepseek-settings-preview.png" width="720" alt="DeepSeekHarness 设置" />
<img src="docs/deepseek-details-preview.png" width="375" alt="模型用量详情" />

以上插图使用模拟数据。

## 开始使用

1. 运行安装包选择安装目录，或完整解压便携包后运行 `CodexQuotaBar.exe`。无需额外安装 .NET 或 C++ 运行库。
2. 使用 Codex 额度前，在本机 Codex CLI 中通过 ChatGPT 账户登录。自动识别失败时，在“高级”页指定 `codex.exe`。
3. 使用 DeepSeek 组件时，在“DeepSeekHarness”页填写平台登录凭证 `userToken` 并保存。可通过页面中的“凭证填写说明”查看获取步骤。
4. 悬浮查看详情，右键打开设置，按住左键横向拖动组件。

额度属于 Codex CLI 当前登录账户。关闭应用窗口或最小化不等于退出进程；后台监听器会等待应用再次启动。

## 配置与操作

- **额度选择**：在“常规”页分别开关 5 小时与周额度，或恢复套餐默认。默认 Plus 显示两项，Pro 系列显示周额度。
- **布局**：自动避让默认开启，空间不足时暂时隐藏组件。拖动后可交换位置，位置在本次运行中保留。
- **DeepSeek 显示**：默认自动跟随 Harness；开启“未启动时也显示余额”后可独立显示和刷新。
- **凭证**：`userToken` 保存在 Windows 凭据管理器。输入留空保留已有值，“清除凭证”在保存后生效。
- **退出与卸载**：“高级”页提供退出及卸载入口。卸载保留程序目录中的无关文件；便携版卸载需要系统 PowerShell。

完整操作说明见 [使用指南](native/USER_GUIDE.md)。

## 数据与适用范围

Codex 额度通过本地 `codex app-server` 读取；DeepSeek 余额和用量通过平台接口读取，当日统计按 UTC+8 切分，不同币种分别显示。设置保存在 `%LOCALAPPDATA%\CodexQuotaBar`，DeepSeek 凭证单独存入 Windows 凭据管理器。

主要适配 Windows 标准水平任务栏。自动隐藏、竖直和第三方任务栏尚未专项验证；上游接口、凭证有效期和系统安全策略可能影响读取或卸载。数据可能存在更新延迟。程序尚未代码签名。

这是独立工具，与 OpenAI、DeepSeek 无隶属关系。

## 开发

[构建与测试](docs/DEVELOPMENT.md) · [安装器](installer/README.md) · [MIT 许可证](LICENSE)
