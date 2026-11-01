param([Parameter(Mandatory)][string]$InnoCompiler, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-native.ps1') }
$payload = Join-Path $root 'artifacts/native-payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($file in @('CodexQuotaBar.exe','version.json','LICENSE','LICENSE-json.txt')) {
    Copy-Item -LiteralPath (Join-Path $root ('artifacts/native-win-x64/' + $file)) -Destination $payload -Force
}
foreach ($file in @('CHANGELOG.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $payload -Force
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $root 'artifacts/native-win-x64') -Force
}
foreach ($folder in @($payload,(Join-Path $root 'artifacts/native-win-x64'))) {
    Copy-Item -LiteralPath (Join-Path $root 'native/USER_GUIDE.md') -Destination (Join-Path $folder 'README.md') -Force
}
if (@(Get-ChildItem -LiteralPath $payload -Directory).Count) { throw 'Native payload must not contain runtime directories' }
& $InnoCompiler '/DAppVersion=0.2.1' ('/DPayloadDir=' + $payload) (Join-Path $root 'installer/setup.iss')
if ($LASTEXITCODE -ne 0) { throw 'Native installer compilation failed' }
python (Join-Path $PSScriptRoot 'package-native.py')
if ($LASTEXITCODE -ne 0) { throw 'Native ZIP packaging failed' }
