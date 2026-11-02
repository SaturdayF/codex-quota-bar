# 原生版开发与验证

0.2.0 使用 C++17、Win32 和 Windows 自带 GDI+，无需 .NET。C++ 标准库和线程运行库静态链接。历史 C# 源码已从当前工作树清理；完整旧构建流程可从 `v0.1.0-installer.1` 标签查阅。

## 构建

需要 Windows 和 x64 MinGW-w64（已用 GCC 11.2.0 验证），将 `g++`、`windres` 加入 PATH；Python 仅用于 ZIP 打包。运行程序不需要这些开发工具。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-native.ps1
```

输出 `artifacts/native-win-x64/CodexQuotaBar.exe`。脚本增量编译，将目标文件放在忽略的 artifacts 中；清理该目录后可完整重建。源码风格使用根目录 `.clang-format`。JSON 解析使用固定版本 nlohmann/json 3.11.3，MIT 许可随程序打包。

## 检查

```powershell
$app = (Resolve-Path artifacts/native-win-x64/CodexQuotaBar.exe).Path
Start-Process $app -ArgumentList '--self-test --output codex_tmp/native-checks.txt' -Wait
Start-Process $app -ArgumentList '--ui-test --output codex_tmp/native-ui' -Wait
Start-Process $app -ArgumentList '--render-ui codex_tmp/native-previews' -Wait
```

核心检查涵盖额度窗口筛选、异常值、旧配置兼容、未知配置字段保留、动画重置、所有样式在多个 DPI / Pro 布局下的实际渲染像素、真实子进程 JSON-RPC 管道和原生卸载标记。

UI 检查在已连接的显示屏创建真实窗口，以合成消息和注入坐标测试右键设置、首帧居中、悬浮恢复、样式切换、色板、缩放、隐藏与最小化恢复，不移动用户鼠标、不写用户设置、不改启动项。测试用额度为模拟数据。隐藏启动测试覆盖后台启动标志影响首次设置窗口显示的情况。0.2.1 新增缩短窗口后多位置滚动检查，对比移动后子窗口的实际像素与完整重绘结果，避免仅用 WM_PRINTCLIENT 掩盖残影。设置布局使用无像素复制的定位，并在完成后重绘所有子控件。

设置与详情使用 Windows Codex 系统 UI 字体栈对应的 Segoe UI，中文使用 Microsoft YaHei UI，开启灰度文字抗锯齿。没有复制或分发 Codex 的私有字体资源。

```powershell
# 只读实际账户额度；输出属于私人数据，不提交到仓库
Start-Process $app -ArgumentList '--probe --output codex_tmp/native-probe.json' -Wait
# 模拟任务栏展示，七秒后退出
Start-Process $app -ArgumentList '--preview --smoke-ui' -Wait
```

`--probe` 只调用 initialize / account/read / account/rateLimits/read，不创建聊天。真实服务端的周期重置不人为触发，动画重置逻辑由模拟输入验证。

## 套餐识别

本机 CLI `0.164.0-alpha.11.1` 通过 `codex app-server generate-json-schema --out codex_tmp/codex-schema` 导出的 `GetAccountResponse.json` 定义以下 `PlanType` 值；该枚举随 CLI 版本变化，不将未识别的新值改写为已知套餐。

```text
free, go, plus, pro, prolite, promax, team,
self_serve_business_prolite, self_serve_business_usage_based,
business, ent26, enterprise_cbp_automation, enterprise_cbp_usage_based,
enterprise, edu, edu_plus, edu_pro, sci, sci_plus, unknown
```

`pro` / `prolite` / `promax` 统一遵循本工具的 Pro 单周展示偏好，这不是对官方套餐限额的承诺。其余套餐若响应有周窗口而无 300 分钟窗口，也使用单周布局。缺失额度不用于推断套餐名称；未知值原样展示。模拟 UI 回归覆盖从 Plus 切换到三个 Pro 值、未知单周套餐，再切回 Plus 的任务栏和详情布局；真实账户只读查询输出保存在忽略目录，不提交个人额度数据。

## 安装与打包

使用官方 Inno Setup 6.7.4（当前已验证），或兼容版本。安装器同样不需要 .NET。运行负载只有 EXE、版本、说明和许可证，不含 .NET 文件夹或 DLL。

```powershell
./scripts/build-native-installer.ps1 -InnoCompiler C:/path/to/ISCC.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/NativeInstaller.Tests.ps1
```

生成 `artifacts/releases/v0.2.1` 中的 Setup EXE、Portable ZIP 和 SHA256SUMS.txt。测试真实安装到带中文和空格的独立目录，再执行原生卸载，检查保留无关文件；`/ISOLATEDTEST=1` 禁止修改用户启动项、快捷方式、卸载注册项、设置和既有监听器。绝不对用户真实安装运行卸载测试。

0.2.0 默认使用自己的版本目录，不把旧 .NET 目录混入新负载。旧安装目录保留，设置继续使用 `%LOCALAPPDATA%/CodexQuotaBar/settings.json`。安装版设置中的卸载按钮只认同目录、相同产品与版本的 Inno 卸载器，启动前再次确认。卸载旧安装前退出程序；旧卸载器可能清理共用的启动项，可在新版本中重新启用。

旧 .NET 发布脚本、测试和未采用的图标方案已清理，历史版本通过 `v0.1.0-installer.1` 标签恢复。`scripts/design-icons.py` 只生成已选定的白底双环图标。

## 发布与限制

原始 `v0.1.0` 和 `v0.1.0-installer.1` 标签及 0.1.0 下载不变；原生版使用新标签。发布前检查 EXE PE 导入表只有系统 DLL、CLR 目录为零；核对上传资产 SHA-256。

多屏验证仅覆盖本机已连接屏幕；自动隐藏、竖直或第三方任务栏、其他设备尚未专项验证。额度组件是任务栏的透明自绘子窗口，跟随系统任务栏层级，避免快速设置打开时被遮挡；无任务栏时回退到置顶弹窗。控制器检测任务栏窗口失效或更换并重新创建组件。详情使用独立顶层窗口，避免被任务栏边界裁剪，标题右侧展示实际读取的套餐类型。UI 检查还覆盖嵌入关系、拖动坐标、焦点保持，以及独立任务栏替身升层、销毁和重建，不重启真实 Explorer。快速设置真实面板仍需手动回归。

程序尚未代码签名，依赖已登录的本机 Codex CLI 和上游 app-server 协议。
