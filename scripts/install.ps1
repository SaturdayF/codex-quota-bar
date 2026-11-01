param([switch]$NoLaunch)
$ErrorActionPreference = 'Stop'
$sourceExe = Join-Path $PSScriptRoot 'CodexQuotaBar.exe'
if (-not (Test-Path -LiteralPath $sourceExe)) { throw 'Run install.ps1 from a published package beside CodexQuotaBar.exe.' }
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs/CodexQuotaBar'
$destination = Join-Path $installRoot '0.1.0'
$existing = Get-Process -Name CodexQuotaBar -ErrorAction SilentlyContinue
foreach ($process in $existing) {
    try {
        if ($process.Path -and $process.Path.StartsWith($installRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            Stop-Process -Id $process.Id
            if (-not $process.WaitForExit(5000)) { throw 'The installed listener did not exit in time.' }
        }
    } catch { throw 'Close the installed Codex Quota Bar before upgrading.' }
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($file in @('CodexQuotaBar.exe','CodexQuotaBar.dll','CodexQuotaBar.deps.json','CodexQuotaBar.runtimeconfig.json','uninstall.ps1','README.md','LICENSE')) {
    $source = Join-Path $PSScriptRoot $file
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $destination -Force }
}
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'runtime')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'runtime') -Destination $destination -Recurse -Force }
$installedExe = Join-Path $destination 'CodexQuotaBar.exe'
$runKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
try { $runKey.SetValue('CodexQuotaBar', ('"' + $installedExe + '"'), [Microsoft.Win32.RegistryValueKind]::String) }
finally { $runKey.Dispose() }
$menuDirectory = Join-Path ([Environment]::GetFolderPath('Programs')) 'Codex Quota Bar'
New-Item -ItemType Directory -Path $menuDirectory -Force | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $menuDirectory 'Codex Quota Bar Settings.lnk'))
$shortcut.TargetPath = $installedExe
$shortcut.Arguments = '--settings'
$shortcut.Save()
if (-not $NoLaunch) { Start-Process -FilePath $installedExe -WindowStyle Hidden }
Write-Host "Installed: $installedExe"
Write-Host 'The lightweight listener now follows ChatGPT/Codex automatically.'
