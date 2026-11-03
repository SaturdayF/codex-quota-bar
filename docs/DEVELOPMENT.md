# 开发说明

## 结构

| 目录 | 内容 |
| --- | --- |
| native/ | C++ 程序、配置、绘图、任务栏布局及数据读取 |
| native/vendor/ | nlohmann/json 头文件和许可证 |
| tests/ | 安装与便携卸载集成测试 |
| scripts/ | 构建、打包和发布脚本 |
| installer/ | Inno Setup 配置与翻译 |
| assets/ | 应用图标 |
| docs/ | 使用说明、界面图及版本记录 |

## 构建

需要 Windows、带完整 UI Automation 头文件的 x64 MinGW-w64，以及 Python。0.3.1 使用 w64devkit 2.11.0 / GCC 16.2.0 和 Inno Setup 6.7.4 构建。

```powershell
./scripts/build-native.ps1
./scripts/build-native-installer.ps1 -SkipBuild -InnoCompiler C:/path/to/ISCC.exe
```

可通过 `-Compiler`、`-Windres` 指定工具路径，或通过 `-OutputDirectory` 选择独立构建目录。运行中的 EXE 不能直接覆盖。

程序使用 C++17、Win32 和 GDI+，静态链接 C++ 运行库。输出位于 `artifacts/native-win-x64`，发布包位于 `artifacts/releases/v0.3.1`，包含 Setup EXE、Portable ZIP 和 SHA256SUMS.txt。

## 数据读取

Codex 通过本地 `codex app-server` 的 `account/read`、`account/rateLimits/read` 读取账户和额度。按所选刷新间隔轮询，并处理更新事件。

DeepSeek 使用 WinHTTP 读取 `platform.deepseek.com` 的账户余额、费用和 Token 接口。账单按 UTC+8 的当日范围聚合；金额使用定点数，不同币种分别累计。请求设置超时、取消和失败退避，响应按配置代次与应用会话校验。凭证保存在 Windows 凭据管理器。

## 窗口与渲染

任务栏组件使用透明自绘窗口。UI Automation worker 获取可见控件边界，布局器联合安排两个组件的空位。任务栏宿主变化时重建组件；嵌入被拒绝时按任务栏矩形定位备用窗口，并记录宿主以避免重复重建。

拖动落位采用约 240 毫秒的减速过渡，支持中途接管。设置页使用固定控件画布和裁剪视口，滚动只移动画布；栏目切换批量定位当前控件并重绘。显示比例跟随屏幕 DPI，动画遵循系统设置。

## 验证

```powershell
$app = (Resolve-Path artifacts/native-win-x64/CodexQuotaBar.exe).Path
Start-Process $app -ArgumentList '--self-test --output codex_tmp/core.txt' -Wait
Start-Process $app -ArgumentList '--ui-test --output codex_tmp/ui' -Wait
Start-Process $app -ArgumentList '--render-ui codex_tmp/previews' -Wait
./tests/NativeInstaller.Tests.ps1
./tests/PortableUninstall.Tests.ps1
```

0.3.1 通过 430 项核心检查与 243 项 UI 检查，其中包含 28 项 DeepSeek UI 检查。覆盖额度解析、配置、子进程通信、多 DPI 绘图、滚动重绘、悬浮与右键、布局避让、拖动交换和会话取消。集成测试覆盖自定义路径安装、安装版卸载、便携版卸载及无关文件保留。

UI 与协议回归使用模拟数据。安装卸载测试使用独立目录，禁止对用户真实安装进行卸载测试。

## 打包与发布

安装版通过 `installer.ini` 识别对应产品和版本的 Inno 卸载器。便携包附带 `portable.json` 和 PowerShell 卸载脚本，校验产品和版本后按固定文件清单清理。

发布前核对版本、ZIP 内容、PE 系统依赖和 SHA-256。发布脚本校验远端标签指向及上传文件摘要。已发布标签和下载文件保持固定，后续功能修改使用新版本。

```powershell
python scripts/github-release.py v0.3.1 <commit> <setup> <zip> <sha256-file>
```

## 验证范围

主要适配标准水平任务栏。其他设备、自动隐藏或第三方任务栏未完成专项验证。备用窗口不保证覆盖所有系统浮层；企业策略可能阻止 PowerShell 便携卸载；上游平台接口变化可能影响读取。
