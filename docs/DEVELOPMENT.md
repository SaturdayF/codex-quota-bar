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

程序使用 C++17、Win32 和 GDI+，静态链接 C++ 运行库。输出位于 `artifacts/native-win-x64`，发布包位于 `artifacts/releases/v<版本>`，包含 Setup EXE、Portable ZIP 和 SHA256SUMS.txt。

## 数据读取

Codex 通过本地 `codex app-server` 的 `account/read`、`account/rateLimits/read` 读取账户和额度。按所选刷新间隔轮询，并处理更新事件。

DeepSeek 使用 WinHTTP 读取 `platform.deepseek.com` 的账户余额、费用和 Token 接口。账单按 UTC+8 的当日范围聚合；金额使用定点数，不同币种分别累计。请求设置超时、取消和失败退避，响应按配置代次与应用会话校验。凭证保存在 Windows 凭据管理器。

## 窗口与渲染

任务栏组件使用透明自绘窗口。UI Automation worker 获取可见控件边界，布局器联合安排两个组件的空位。任务栏宿主变化时重建组件；嵌入被拒绝时按任务栏矩形定位备用窗口，并记录宿主以避免重复重建。

拖动落位采用约 240 毫秒的减速过渡，支持中途接管。设置页使用固定控件画布和裁剪视口，滚动只移动画布；栏目切换批量定位当前控件并重绘。显示比例跟随屏幕 DPI，动画遵循系统设置。

### 界面材质与托盘

字体统一为 Segoe UI / Microsoft YaHei UI，无衬线标题与正文共用绘制路径。滑动条保留原生键盘和辅助功能语义，自绘有色轨道及圆形滑块，键盘焦点仅标记滑块。

设置与详情使用 DWM 实时背景合成。设置在 Windows 11 22H2+ 使用 `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`；较早系统通过动态加载的 `SetWindowCompositionAttribute` 启用兼容模糊。详情窗口使用不依赖激活状态的 Accent Acrylic，避免无焦点窗口被系统退回纯色，同时不抢焦点。关闭合成、高对比度或进入省电模式（Battery Saver）时退回纯色——省电模式下系统会关闭半透明与模糊，因此毛玻璃不可用。顶栏保留原生窗口行为，`DwmExtendFrameIntoClientArea` 将同一材质延伸到内容区。DeepSeek「凭证填写说明」使用与设置同款的独立毛玻璃弹层（`CodexQuotaBar.Native.Help`），前景绘制与设置共享 `glassPalette`/`paintFrost`，支持拖动、回车与 Esc 关闭。

DeepSeek「平台登录凭证」支持一键获取：`native/platform_login.cpp` 通过 Windows HTTPS 协议关联识别默认浏览器，使用 Edge / Chrome 最近使用的配置打开普通平台网页，后台通过 `native/browser_token.cpp` 以共享只读方式解析该配置的 Local Storage LevelDB。读取遵循 CURRENT/MANIFEST 的有效表集、WAL 序号与删除标记，校验 CRC32C，支持 Snappy 与 Latin1/UTF16；不会复制浏览器数据库到磁盘，也不会修改或恢复数据库。仅选取 `https://platform.deepseek.com` 的 `userToken`，调用官方平台账户接口验证后立即写入 Windows 凭据管理器、取消旧查询会话并刷新，自动切回 DeepSeek 设置页显示保存状态。此操作只保存取得的凭证，其他设置仍保留为草稿；输入框留空表示保留已保存凭证。读取取消、重复点击、用户编辑和关闭窗口均有生命周期处理。网络失败、登录失效、存储格式不兼容和凭证写入失败分别提示。自动保存检查使用内存存储替身，禁止写入真实测试凭证。暂不支持 InPrivate、自定义用户数据目录和 SQLite 存储。

存储格式参考：[Chromium Local Storage schema](https://chromium.googlesource.com/chromium/src/+/123.0.6312.86/components/services/storage/dom_storage/local_storage_impl.cc)、[LevelDB 日志格式](https://github.com/google/leveldb/blob/main/doc/log_format.md)、[表格式](https://github.com/google/leveldb/blob/main/doc/table_format.md)、[VersionEdit](https://github.com/google/leveldb/blob/main/db/version_edit.cc) 与 [Snappy 格式](https://github.com/google/snappy/blob/main/format_description.txt)。这些实现细节可能随浏览器版本变化；未知格式必须失败并提示手动填写。

GDI+ 表面保留预乘 Alpha，通过 BitBlt 提交到窗口重定向表面。输入框由原生 EDIT 离屏绘制文本、选择与密码遮蔽，移除平面背景后合成到圆角玻璃表面；使用灰度抗锯齿避免 ClearType 在透明材质上产生彩边。文字与光标保持垂直居中。详情使用普通 DWM 窗口并缓存前景绘制结果，背景由合成器持续更新。运行时不截取桌面，不依赖定时截图，也不需要在窗口移动时重新模糊。仅导出预览使用模拟背景。

设置窗口通过 `WM_NCCALCSIZE` 将客户区延伸到顶部，隐藏可见标题与图标，侧栏底色覆盖完整高度；窗口名称仍用于任务切换和辅助功能。`DwmDefWindowProc` 保留原生标题栏按钮，顶部补充拖动及缩放命中检测。下拉框、操作按钮和预览区共用半透明表面、细高光边框和悬停/按下状态。

展开列表保留原生 ComboLBox 的键盘、选择和关闭行为，通过子类绘制圆角选中行与当前选项标记；弹层显示时初始化 Accent Acrylic。DeepSeek 的 `DeepSeekShowBadge` 默认开启，关闭时布局与绘制同时移除标识占用的空间，保留余额和消费文本。

实现参考：[Microsoft 系统背景属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type)、[DWM 自定义窗口框架](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)和 [window-vibrancy](https://github.com/tauri-apps/window-vibrancy) 的平台兼容说明。未引入第三方图形运行库。

`native/glass_tests.cpp` 使用独立的纯色/分区背景窗口，验证浅色和深色内容、原生顶栏、下拉弹层、输入框、移动后的背景变化及详情窗口实时更新，并生成仅含模拟数据的合成截图。覆盖菜单键盘操作、Unicode 编辑和撤销、等长凭证遮蔽。Windows 10 的兼容路径仍需在对应系统实机验证。

托盘使用 Shell_NotifyIcon v4，接收 TaskbarCreated 重建通知，首次注册失败时每 5 秒重试。退出时删除图标，测试和渲染模式不注册真实托盘图标。UI 检查通过模拟 Shell 调用验证注册、重试、恢复和清理，避免重启用户的 Explorer。

## 验证

```powershell
$app = (Resolve-Path artifacts/native-win-x64/CodexQuotaBar.exe).Path
Start-Process $app -ArgumentList '--self-test --output codex_tmp/core.txt' -Wait
Start-Process $app -ArgumentList '--ui-test --output codex_tmp/ui' -Wait
Start-Process $app -ArgumentList '--render-ui codex_tmp/previews' -Wait
./tests/NativeInstaller.Tests.ps1
./tests/PortableUninstall.Tests.ps1
```

0.3.6 通过 472 项核心检查与 54 项 DeepSeek UI 检查。检查覆盖额度解析、配置、子进程通信、多 DPI 绘图、滚动重绘、悬浮与右键、布局避让、拖动交换和会话取消；DeepSeek 覆盖北京时间小时分桶、凭证弹窗生命周期、Edge 本地存储、自动保存与失败处理及 DSH 标识。集成测试覆盖自定义路径安装、安装版卸载、便携版卸载及无关文件保留。完整 UI 回归需在常规桌面环境执行，本机虚拟显示会限制实时毛玻璃验证。

UI 与协议回归使用模拟数据。安装卸载测试使用独立目录，禁止对用户真实安装进行卸载测试。

## 打包与发布

安装版通过 `installer.ini` 识别对应产品的 Inno 卸载器。便携版使用内置清理流程，等待程序退出后按 `uninstall-manifest.json` 校验和清理随附文件。测试分别覆盖清理进程和实际打包清单。

发布前核对版本、ZIP 内容、PE 系统依赖和 SHA-256。发布脚本校验远端标签指向及上传文件摘要。已发布标签和下载文件保持固定，后续功能修改使用新版本。

```powershell
python scripts/github-release.py v0.3.4 <commit> <setup> <zip> <sha256-file>
```

## 验证范围

主要适配标准水平任务栏。其他设备、自动隐藏或第三方任务栏未完成专项验证。备用窗口不保证覆盖所有系统浮层；企业策略可能阻止 PowerShell 便携卸载；上游平台接口变化可能影响读取。
