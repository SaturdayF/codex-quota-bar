param([string]$Archive)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Archive) { $Archive = Join-Path $root 'artifacts/releases/v0.3.1/CodexQuotaBar-v0.3.1-Portable-win-x64.zip' }
$fixture = Join-Path $root ('codex_tmp/portable-uninstall-' + [Guid]::NewGuid().ToString('N'))
$target = Join-Path $fixture "便携 空格 ' 测试"
New-Item -ItemType Directory -Path $target -Force | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $target
Set-Content -LiteralPath (Join-Path $target 'keep.txt') -Value 'unrelated'
$helper = Join-Path $target 'portable-uninstall.ps1'
$p = Start-Process powershell.exe -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',('"'+$helper+'"'),'-InstallDirectory',('"'+$target+'"'),'-IsolatedTest') -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0) { throw 'Portable uninstall helper failed' }
if (-not (Test-Path -LiteralPath (Join-Path $target 'keep.txt'))) { throw 'Unrelated portable file removed' }
if (@(Get-ChildItem -LiteralPath $target -Force).Count -ne 1) { throw 'Portable payload remains' }
'PASS portable uninstall removes its own payload and script at Unicode, space and quote path; preserves unrelated files'
$resolved = [IO.Path]::GetFullPath($fixture)
if ((Split-Path $resolved -Parent) -ne (Join-Path $root 'codex_tmp')) { throw 'Unsafe fixture cleanup' }
Remove-Item -LiteralPath $resolved -Recurse -Force
