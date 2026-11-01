$ErrorActionPreference = 'Stop'
$installRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs/CodexQuotaBar'))
$programsRoot = [System.IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs'))
if ((Split-Path -Parent $installRoot) -ne $programsRoot -or (Split-Path -Leaf $installRoot) -ne 'CodexQuotaBar') { throw 'Unexpected installation directory.' }
foreach ($process in (Get-Process -Name CodexQuotaBar -ErrorAction SilentlyContinue)) {
    if ($process.Path -and $process.Path.StartsWith($installRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) { Stop-Process -Id $process.Id }
}
$runKey = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run'
$entry = (Get-ItemProperty -Path $runKey -Name CodexQuotaBar -ErrorAction SilentlyContinue).CodexQuotaBar
if ($entry -and $entry.Contains($installRoot)) { Remove-ItemProperty -Path $runKey -Name CodexQuotaBar }
$menuDirectory = [System.IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('Programs')) 'Codex Quota Bar'))
$menuRoot = [System.IO.Path]::GetFullPath([Environment]::GetFolderPath('Programs'))
if ((Split-Path -Parent $menuDirectory) -ne $menuRoot -or (Split-Path -Leaf $menuDirectory) -ne 'Codex Quota Bar') { throw 'Unexpected shortcut directory.' }
$shortcut = Join-Path $menuDirectory 'Codex Quota Bar Settings.lnk'
if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut }
if ((Test-Path -LiteralPath $menuDirectory) -and -not (Get-ChildItem -LiteralPath $menuDirectory -Force)) { Remove-Item -LiteralPath $menuDirectory }
# Only remove this installer's known version; do not recurse over unrelated or future versions.
$versionDirectory = [System.IO.Path]::GetFullPath((Join-Path $installRoot '0.1.0'))
if ((Split-Path -Parent $versionDirectory) -ne $installRoot) { throw 'Unexpected version directory.' }
if (Test-Path -LiteralPath $versionDirectory) { Remove-Item -LiteralPath $versionDirectory -Recurse -Force }
if ((Test-Path -LiteralPath $installRoot) -and -not (Get-ChildItem -LiteralPath $installRoot -Force)) { Remove-Item -LiteralPath $installRoot }
Write-Host 'Removed. User settings under LOCALAPPDATA/CodexQuotaBar were retained.'
