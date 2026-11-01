# 开发与验证

## 构建

需要 Windows、.NET 10 SDK。运行代码无第三方 NuGet 依赖。

```powershell
dotnet build -c Release
dotnet run --no-build -c Release -- --self-test
dotnet run --no-build -c Release -- --hover-test
dotnet run --no-build -c Release -- --settings-test
dotnet run --no-build -c Release -- --layout-test
```

`--self-test` 检查解析、状态、几何、颜色、动画、配置和真实子进程 JSON-RPC。其余检查使用原生窗口与合成消息，不移动鼠标；设置和布局检查需要至少一个 ChatGPT / Codex 桌面应用运行，双屏覆盖需要连接两块屏幕。测试不保存设置、不执行真正卸载。

`--probe` 为只读实际额度查询，输出的额度与套餐属于本机账户数据，不要提交到仓库。

## 卸载验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1 -WhatIf -RemoveSettings
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\Uninstall.Tests.ps1
```

预演列出目标，不结束进程、不删文件、不改注册表。卸载按钮仅在程序正从当前用户对应版本的安装目录运行且卸载脚本存在时可用。按钮有二次确认；独立 PowerShell 辅助进程等待 UI 退出，再停止其余已安装的本工具进程、删除本版本和可选设置，最后清理自己的临时脚本。

卸载测试在可删除的临时目录实际执行文件清理，模拟系统进程、注册表和快捷方式根目录；验证预演不写入、完整删除本版本、保留其他版本、默认保留设置及拒绝非法版本路径，不触碰用户真实安装。

## 插图与图标

```powershell
dotnet run -c Release -- --render-ui docs
dotnet run -c Release -- --render-motion codex_tmp/animation-frames
python scripts/make-animation.py codex_tmp/animation-frames docs/animation-preview.gif
python scripts/design-icons.py
```

Python 素材工具需要 Pillow，仅开发时使用。图标脚本依赖 Windows 字体目录，生成可编辑 SVG、应用 PNG/多尺寸 ICO 和候选对照图。C# 原生渲染使用模拟额度，不应替换为真实账户数据。生成动画后可删除临时帧，保留 GIF 与生成代码。

## 打包

用户随后要求将可选目录的安装器与小 ZIP 也归入 0.1.0。此补充使用 `v0.1.0-installer.1` 源码标签，原 `v0.1.0` 标签不动；经明确授权替换该 Release 的大 ZIP 下载。

```powershell
./scripts/build-installer.ps1 -InnoCompiler C:/path/to/ISCC.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/Installer.Tests.ps1
```

详见 [安装器说明](../installer/README.md)。完整 EXE 使用 LZMA2 压缩，包含私有运行时；Portable ZIP 不含运行时，需全局 .NET 10 Desktop Runtime x64。两者不是相同依赖条件下的等价包。

```powershell
./scripts/publish.ps1
# 已有离线 SDK/运行时的备选方式
./scripts/publish.ps1 -BundledDotnetRoot C:/path/to/dotnet-sdk
python scripts/package-release.py
```

默认生成 Windows x64 自包含程序。离线方式把既有 SDK 中的 .NET 和 Windows Desktop 运行时复制到包内的 `runtime`，无需用户安装全局 .NET。两者均输出到 `artifacts/win-x64`。安装脚本与卸载脚本读取生成的版本清单；程序集版本和清单源于 `version.props`。

`package-release.py` 生成 `artifacts/releases/vX.Y.Z` 下的 ZIP 和 SHA-256 文件，排除调试 PDB，并拒绝覆盖已有 ZIP。

打包目录应包含 EXE、必要 DLL/JSON/运行时、安装和卸载脚本、README、CHANGELOG、LICENSE、文档插图与 `version.json`。不要把本机设置、登录材料、调试 PDB 或私有 SDK 本体放入发布 ZIP。

## 发布规则

完成测试和素材检查后提交，创建带说明的 `vX.Y.Z` Git 标签；发布完整 ZIP 及其 SHA-256 校验文件。`v0.1.0` 封存后不得重写标签、修改该版本发布包。后续修改先将版本提升至 `0.2.0`，并在 CHANGELOG 中记录，不直接修补旧版本。

本次 0.1.0 安装器补充及下载替换是用户明确要求的例外。未来其他功能仍按 0.2.0 推进。
