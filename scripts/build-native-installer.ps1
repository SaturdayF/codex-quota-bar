param([Parameter(Mandatory)][string]$InnoCompiler, [switch]$SkipBuild, [string]$Python = 'python', [string]$RuntimeDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $root 'artifacts/native-win-x64' }
$RuntimeDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RuntimeDirectory)
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-native.ps1') -OutputDirectory $RuntimeDirectory }
$version = Get-Content -LiteralPath (Join-Path $RuntimeDirectory 'version.json') -Raw | ConvertFrom-Json
if ($version.version -ne '0.3.5' -or (Get-Item -LiteralPath (Join-Path $RuntimeDirectory 'CodexQuotaBar.exe')).VersionInfo.FileVersion -ne '0.3.5') { throw 'Build a matching 0.3.5 runtime before packaging' }
& $InnoCompiler '/DAppVersion=0.3.5' ('/DPayloadDir=' + $RuntimeDirectory) (Join-Path $root 'installer/setup.iss')
if ($LASTEXITCODE -ne 0) { throw 'Native installer compilation failed' }
& $Python (Join-Path $PSScriptRoot 'package-native.py') $RuntimeDirectory
if ($LASTEXITCODE -ne 0) { throw 'Native ZIP packaging failed' }
