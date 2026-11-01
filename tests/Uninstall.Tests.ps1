# Exercise real file removal in a disposable fixture; OS process/registry calls are mocked.
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$fixture = [IO.Path]::GetFullPath((Join-Path $project ('codex_tmp/uninstall-test-' + [Guid]::NewGuid().ToString('N'))))
$temporaryRoot = [IO.Path]::GetFullPath((Join-Path $project 'codex_tmp'))
if ((Split-Path -Parent $fixture) -ne $temporaryRoot) { throw 'Unsafe fixture root.' }
$oldLocal = $env:LOCALAPPDATA
$menu = Join-Path $fixture 'Shortcuts'
$global:quotaTestVersionDirectory = [IO.Path]::GetFullPath((Join-Path $fixture 'Local/Programs/CodexQuotaBar/0.1.0'))
$future = [IO.Path]::GetFullPath((Join-Path $fixture 'Local/Programs/CodexQuotaBar/0.2.0'))
$data = [IO.Path]::GetFullPath((Join-Path $fixture 'Local/CodexQuotaBar'))
$global:quotaTestStartupRemoved = $false
function Get-Process { param($Name, $Id, $ErrorAction) return @() }
function Get-ItemProperty { param($Path, $Name, $ErrorAction) return [pscustomobject]@{ CodexQuotaBar = '"' + (Join-Path $global:quotaTestVersionDirectory 'CodexQuotaBar.exe') + '"' } }
function Remove-ItemProperty { param($Path, $Name) if ($Name -ne 'CodexQuotaBar') { throw 'Unexpected registry target' }; $global:quotaTestStartupRemoved = $true }
function Remove-Item {
    param([string]$LiteralPath, [switch]$Recurse, [switch]$Force)
    $target = [IO.Path]::GetFullPath($LiteralPath)
    if (-not $target.StartsWith($fixture + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Removal escaped fixture.' }
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $target -Recurse:$Recurse -Force:$Force
}
function Make-Fixture {
    New-Item -ItemType Directory -Path $global:quotaTestVersionDirectory,$future,$data,(Join-Path $menu 'Codex Quota Bar') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $global:quotaTestVersionDirectory 'CodexQuotaBar.exe') -Value 'fixture'
    Set-Content -LiteralPath (Join-Path $future 'keep.txt') -Value 'future version must remain'
    Set-Content -LiteralPath (Join-Path $data 'settings.json') -Value '{}'
    Set-Content -LiteralPath (Join-Path $menu 'Codex Quota Bar/Codex Quota Bar Settings.lnk') -Value 'fixture'
}
try {
    $env:LOCALAPPDATA = Join-Path $fixture 'Local'
    Make-Fixture
    # Replace only the OS shortcut-directory lookup. All production deletion logic is unchanged.
    $source = Get-Content -LiteralPath (Join-Path $project 'scripts/uninstall.ps1') -Raw
    $lookup = "[Environment]::GetFolderPath('Programs')"
    if (-not $source.Contains($lookup)) { throw 'Shortcut lookup changed; review fixture seam.' }
    $source = $source.Replace($lookup, ("'" + $menu.Replace("'", "''") + "'"))
    $copy = Join-Path $fixture 'uninstall.ps1'
    [IO.File]::WriteAllText($copy, $source, [Text.UTF8Encoding]::new($true))
    & $copy -Version 0.1.0 -RemoveSettings -WhatIf
    if (-not (Test-Path -LiteralPath $global:quotaTestVersionDirectory) -or -not (Test-Path -LiteralPath $data) -or $global:quotaTestStartupRemoved) { throw 'WhatIf changed fixture' }
    Write-Output 'PASS WhatIf preserves files and startup'
    & $copy -Version 0.1.0 -RemoveSettings
    if ((Test-Path -LiteralPath $global:quotaTestVersionDirectory) -or (Test-Path -LiteralPath $data) -or (Test-Path -LiteralPath (Join-Path $menu 'Codex Quota Bar')) -or -not $global:quotaTestStartupRemoved -or -not (Test-Path -LiteralPath (Join-Path $future 'keep.txt'))) { throw 'Full uninstall fixture mismatch' }
    Write-Output 'PASS removes installed version, shortcut, settings and startup; preserves other version'
    Make-Fixture
    & $copy -Version 0.1.0
    if ((Test-Path -LiteralPath $global:quotaTestVersionDirectory) -or -not (Test-Path -LiteralPath $data)) { throw 'Settings retention mismatch' }
    Write-Output 'PASS script default retains settings'
    $invalidRejected = $false
    try { & $copy -Version '../other' } catch { $invalidRejected = $true }
    if (-not $invalidRejected) { throw 'Unsafe version accepted' }
    Write-Output 'PASS invalid version path rejected'
} finally {
    $env:LOCALAPPDATA = $oldLocal
    # This task created and validated the entire fixture; remove it with one native shell.
    if ((Split-Path -Parent $fixture) -eq $temporaryRoot -and (Test-Path -LiteralPath $fixture)) {
        Microsoft.PowerShell.Management\Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
