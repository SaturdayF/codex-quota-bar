param([string]$Archive)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Archive) { $Archive = Join-Path $root 'artifacts/releases/v0.3.2/CodexQuotaBar-v0.3.2-Portable-win-x64.zip' }
$fixture = Join-Path $root ('codex_tmp/portable-uninstall-' + [Guid]::NewGuid().ToString('N'))
$target = Join-Path $fixture "便携 空格 ' 测试"
New-Item -ItemType Directory -Path $target -Force | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $target
Set-Content -LiteralPath (Join-Path $target 'keep.txt') -Value 'unrelated'
$planPath = Join-Path $fixture 'plan.json'
$app = Join-Path $target 'CodexQuotaBar.exe'
$p = Start-Process -FilePath $app -ArgumentList @('--uninstall-plan','--output',('"' + $planPath + '"')) -WindowStyle Hidden -Wait -PassThru
if ($p.ExitCode -ne 0) { throw 'Portable uninstall planning failed' }
$plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
$expected = @(Get-ChildItem -LiteralPath $target -File | Where-Object Name -ne 'keep.txt' | Select-Object -ExpandProperty Name)
if (Compare-Object $expected @($plan.files.name)) { throw 'Packaged uninstall plan does not match the payload' }
if (-not (Test-Path -LiteralPath (Join-Path $target 'keep.txt'))) { throw 'Unrelated portable file removed' }
'PASS packaged uninstall dry run recognizes every shipped file and excludes unrelated files'
$resolved = [IO.Path]::GetFullPath($fixture)
if ((Split-Path $resolved -Parent) -ne (Join-Path $root 'codex_tmp')) { throw 'Unsafe fixture cleanup' }
Remove-Item -LiteralPath $resolved -Recurse -Force
