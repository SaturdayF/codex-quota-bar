param([Parameter(Mandatory)][string]$InnoCompiler, [string]$BundledDotnetRoot)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'publish.ps1') -BundledDotnetRoot $BundledDotnetRoot
$payload = Join-Path $project 'artifacts/win-x64'
$portable = Join-Path $project 'artifacts/portable-win-x64'
dotnet publish (Join-Path $project 'CodexQuotaBar.csproj') -c Release -r win-x64 --self-contained false -p:NuGetAudit=false --ignore-failed-sources -o $portable
if ($LASTEXITCODE -ne 0) { throw 'Portable build failed' }
foreach ($file in @('README.md','CHANGELOG.md','LICENSE','version.json')) { Copy-Item -LiteralPath (Join-Path $payload $file) -Destination $portable -Force }
foreach ($folder in @('docs','assets','installer')) { Copy-Item -LiteralPath (Join-Path $payload $folder) -Destination $portable -Recurse -Force }
'This small ZIP requires .NET 10 Desktop Runtime x64 already installed. For offline installation, download the Setup.exe instead.' | Set-Content -LiteralPath (Join-Path $portable 'PORTABLE.txt') -Encoding UTF8
python (Join-Path $PSScriptRoot 'package-portable.py')
if ($LASTEXITCODE -ne 0) { throw 'Portable packaging failed' }
$version = ([xml](Get-Content -LiteralPath (Join-Path $project 'version.props') -Raw)).Project.PropertyGroup.Version
& $InnoCompiler ('/DAppVersion=' + $version) ('/DPayloadDir=' + $payload) (Join-Path $project 'installer/setup.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
