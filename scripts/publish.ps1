param([string]$Runtime = 'win-x64', [string]$BundledDotnetRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDirectory = Join-Path $projectRoot "artifacts/$Runtime"
if ($BundledDotnetRoot) {
    # Offline fallback using an already verified Microsoft SDK/runtime distribution.
    $runtimeRoot = [System.IO.Path]::GetFullPath($BundledDotnetRoot)
    foreach ($required in @('dotnet.exe','host/fxr','shared/Microsoft.NETCore.App','shared/Microsoft.WindowsDesktop.App')) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $required))) { throw "Missing runtime component: $required" }
    }
    dotnet publish (Join-Path $projectRoot 'CodexQuotaBar.csproj') -c Release -r $Runtime --self-contained false -p:AppHostRelativeDotNet=runtime -p:NuGetAudit=false --ignore-failed-sources -o $outputDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Publish failed' }
    $privateRuntime = Join-Path $outputDirectory 'runtime'
    New-Item -ItemType Directory -Path (Join-Path $privateRuntime 'shared') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $privateRuntime 'host') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'dotnet.exe') -Destination $privateRuntime -Force
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'host/fxr') -Destination (Join-Path $privateRuntime 'host') -Recurse -Force
    foreach ($framework in @('Microsoft.NETCore.App','Microsoft.WindowsDesktop.App')) {
        Copy-Item -LiteralPath (Join-Path $runtimeRoot "shared/$framework") -Destination (Join-Path $privateRuntime 'shared') -Recurse -Force
    }
    foreach ($notice in @('LICENSE.txt','ThirdPartyNotices.txt')) {
        if (Test-Path -LiteralPath (Join-Path $runtimeRoot $notice)) { Copy-Item -LiteralPath (Join-Path $runtimeRoot $notice) -Destination $privateRuntime -Force }
    }
} else {
    dotnet publish (Join-Path $projectRoot 'CodexQuotaBar.csproj') -c Release -r $Runtime --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:NuGetAudit=false -o $outputDirectory
}
if ($LASTEXITCODE -ne 0) { throw 'Publish failed' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/install.ps1') -Destination $outputDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts/uninstall.ps1') -Destination $outputDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $outputDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $outputDirectory
Write-Host "Output: $outputDirectory"
