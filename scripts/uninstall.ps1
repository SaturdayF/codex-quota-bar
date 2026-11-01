[CmdletBinding(SupportsShouldProcess)]
param([int]$WaitForProcessId = 0, [string]$Version, [switch]$RemoveSettings, [switch]$DeleteHelper, [switch]$ShowResult)
$ErrorActionPreference = 'Stop'
if (-not $Version) {
    $manifest = Join-Path $PSScriptRoot 'version.json'
    $sourceVersion = Join-Path $PSScriptRoot '../version.props'
    if (Test-Path -LiteralPath $manifest) { $Version = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).version }
    elseif (Test-Path -LiteralPath $sourceVersion) { $Version = ([xml](Get-Content -LiteralPath $sourceVersion -Raw)).Project.PropertyGroup.Version }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Missing or invalid version.' }
$localRoot = [System.IO.Path]::GetFullPath($env:LOCALAPPDATA)
$installRoot = [System.IO.Path]::GetFullPath((Join-Path $localRoot 'Programs/CodexQuotaBar'))
$versionDirectory = [System.IO.Path]::GetFullPath((Join-Path $installRoot $Version))
$dataDirectory = [System.IO.Path]::GetFullPath((Join-Path $localRoot 'CodexQuotaBar'))
$menuRoot = [System.IO.Path]::GetFullPath([Environment]::GetFolderPath('Programs'))
$menuDirectory = [System.IO.Path]::GetFullPath((Join-Path $menuRoot 'Codex Quota Bar'))
# Validate every absolute recursive target before any mutation.
if ((Split-Path -Parent $installRoot) -ne (Join-Path $localRoot 'Programs') -or
    (Split-Path -Leaf $installRoot) -ne 'CodexQuotaBar' -or
    (Split-Path -Parent $versionDirectory) -ne $installRoot -or
    (Split-Path -Leaf $versionDirectory) -ne $Version -or
    (Split-Path -Parent $dataDirectory) -ne $localRoot -or
    (Split-Path -Leaf $dataDirectory) -ne 'CodexQuotaBar' -or
    (Split-Path -Parent $menuDirectory) -ne $menuRoot -or
    (Split-Path -Leaf $menuDirectory) -ne 'Codex Quota Bar') { throw 'Unexpected uninstall target.' }
try {
    if ($WaitForProcessId -gt 0 -and -not $WhatIfPreference) {
        $parent = Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
        if ($parent -and -not $parent.WaitForExit(15000)) { throw 'The application did not exit in time. Close it and retry.' }
    }
    foreach ($process in (Get-Process -Name CodexQuotaBar -ErrorAction SilentlyContinue)) {
        if ($process.Path -and $process.Path.StartsWith($installRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase) -and
            $PSCmdlet.ShouldProcess($process.Path, 'Stop installed quota tool')) {
            # Let the listener release its app-server and windows after the quit signal.
            if (-not $process.WaitForExit(3000)) {
                Stop-Process -Id $process.Id
                if (-not $process.WaitForExit(5000)) { throw 'An installed process is still running.' }
            }
        }
    }
    $runKey = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run'
    $entry = (Get-ItemProperty -Path $runKey -Name CodexQuotaBar -ErrorAction SilentlyContinue).CodexQuotaBar
    if ($entry -and $entry.Contains($installRoot) -and $PSCmdlet.ShouldProcess('HKCU Run/CodexQuotaBar', 'Remove startup entry')) {
        Remove-ItemProperty -Path $runKey -Name CodexQuotaBar
    }
    $shortcut = Join-Path $menuDirectory 'Codex Quota Bar Settings.lnk'
    if ((Test-Path -LiteralPath $shortcut) -and $PSCmdlet.ShouldProcess($shortcut, 'Remove shortcut')) { Remove-Item -LiteralPath $shortcut }
    if ((Test-Path -LiteralPath $menuDirectory) -and -not (Get-ChildItem -LiteralPath $menuDirectory -Force) -and $PSCmdlet.ShouldProcess($menuDirectory, 'Remove empty shortcut directory')) { Remove-Item -LiteralPath $menuDirectory }
    if ((Test-Path -LiteralPath $versionDirectory) -and $PSCmdlet.ShouldProcess($versionDirectory, 'Remove installed version')) {
        Remove-Item -LiteralPath $versionDirectory -Recurse -Force
    }
    if ((Test-Path -LiteralPath $installRoot) -and -not (Get-ChildItem -LiteralPath $installRoot -Force) -and $PSCmdlet.ShouldProcess($installRoot, 'Remove empty install directory')) { Remove-Item -LiteralPath $installRoot }
    if ($RemoveSettings -and (Test-Path -LiteralPath $dataDirectory) -and $PSCmdlet.ShouldProcess($dataDirectory, 'Remove quota tool settings and diagnostic log')) {
        Remove-Item -LiteralPath $dataDirectory -Recurse -Force
    }
    Write-Output $(if ($WhatIfPreference) { 'Dry run complete; nothing removed.' } elseif ($RemoveSettings) { 'Removed quota tool and settings.' } else { 'Removed quota tool; settings retained.' })
    if ($ShowResult -and -not $WhatIfPreference) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show('Codex Quota Bar 已卸载。', '卸载完成') | Out-Null
    }
} catch {
    if ($ShowResult -and -not $WhatIfPreference) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show('卸载未完成：' + $_.Exception.Message, 'Codex Quota Bar') | Out-Null
    }
    throw
} finally {
    if ($DeleteHelper -and -not $WhatIfPreference) {
        $helperPath = [System.IO.Path]::GetFullPath($PSCommandPath)
        $helperRoot = Split-Path -Parent $helperPath
        $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
        if ((Split-Path -Parent $helperRoot) -eq $tempRoot -and (Split-Path -Leaf $helperRoot) -match '^CodexQuotaBar-Uninstall-[0-9a-f]{32}$' -and (Split-Path -Leaf $helperPath) -eq 'uninstall.ps1') {
            Remove-Item -LiteralPath $helperPath -Force
            if (-not (Get-ChildItem -LiteralPath $helperRoot -Force)) { Remove-Item -LiteralPath $helperRoot }
        }
    }
}
