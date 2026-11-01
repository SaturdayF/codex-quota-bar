# 安装器

使用 [Inno Setup](https://jrsoftware.org/isinfo.php) 编译。配置为当前用户安装、始终显示目标目录选择页、LZMA2 solid 压缩，并内置所需 .NET 运行时。安装目录需用户可写，默认不请求管理员权限。

```powershell
./scripts/build-installer.ps1 -InnoCompiler 'C:/path/to/ISCC.exe'
# 可选：使用已有离线 .NET SDK/运行时
./scripts/build-installer.ps1 -InnoCompiler 'C:/path/to/ISCC.exe' -BundledDotnetRoot 'C:/path/to/dotnet-sdk'
```

`ChineseSimplified.isl` 来自官方[翻译页面](https://jrsoftware.org/files/istrans/)指向的[简体中文文件](https://raw.githubusercontent.com/jrsoftware/issrc/refs/heads/main/Files/Languages/ChineseSimplified.isl)，原作者说明保留在文件头。已用官方签名的 Inno Setup 6.7.4 编译验证。

原生卸载按安装记录移除文件，保留用户额外放入的文件。应用设置页通过 `installer.ini` 的产品/版本标记识别同目录的原生卸载器；旧 PowerShell 安装继续使用原卸载流程。

`/ISOLATEDTEST=1` 供验证脚本使用：禁用启动项、快捷方式、卸载注册表项、用户设置删除和对现有监听器的停止请求。仍真实安装离线负载并生成原生卸载器，允许测试自定义路径和文件清理。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/Installer.Tests.ps1
```
