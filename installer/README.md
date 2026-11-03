# 安装器

使用 Inno Setup 构建当前用户安装包，支持自定义目录、登录启动、开始菜单和可选桌面设置快捷方式，以及 Windows 卸载入口。安装目录需当前用户可写。

```powershell
./scripts/build-native-installer.ps1 -InnoCompiler C:/path/to/ISCC.exe
```

0.3.1 使用 Inno Setup 6.7.4 验证，采用 LZMA2 压缩。负载包含原生程序、版本信息、使用指南和许可证。

设置页根据同目录 `installer.ini` 的产品标记启动 Inno 卸载器。卸载按安装记录删除文件，保留用户额外文件。

`/ISOLATEDTEST=1` 用于独立目录测试，隔离用户启动项、快捷方式、注册项、设置和现有监听器。

```powershell
./tests/NativeInstaller.Tests.ps1
```

简体中文翻译来自 [Inno Setup 官方翻译](https://jrsoftware.org/files/istrans/)，作者信息保留在文件头。
