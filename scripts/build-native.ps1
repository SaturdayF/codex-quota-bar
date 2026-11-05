param([string]$Compiler = 'g++', [string]$Windres = 'windres', [switch]$DebugBuild, [string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = if ($OutputDirectory) { $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory) } else { Join-Path $root 'artifacts/native-win-x64' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location (Join-Path $root 'native')
try {
    & $Windres '-I.' 'app.rc' '-O' 'coff' '-o' (Join-Path $output 'app.res')
    if ($LASTEXITCODE -ne 0) { throw 'Native resources failed' }
    $options = @('-std=c++17','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-municode','-mwindows','-static','-static-libgcc','-static-libstdc++','-Wall','-Wextra','-Wno-misleading-indentation','-Wno-cast-function-type','-Wno-missing-field-initializers','-Wno-unknown-pragmas','-ffunction-sections','-fdata-sections','-Wl,--gc-sections')
    if ($DebugBuild) { $options += @('-O0','-g') } else { $options += @('-Os','-s') }
    $objects = @()
    $signature = $options -join ' '
    $signatureFile = Join-Path $output 'build-options.txt'
    $sameOptions = (Test-Path -LiteralPath $signatureFile) -and (Get-Content -LiteralPath $signatureFile -Raw).Trim() -eq $signature
    $headers = @(Get-ChildItem -LiteralPath . -Filter '*.hpp' -File) + @(Get-Item -LiteralPath 'vendor/json.hpp')
    foreach ($source in @('core.cpp','uninstall.cpp','taskbar.cpp','deepseek.cpp','deepseek_ui.cpp','deepseek_tests.cpp','browser_token.cpp','drawing.cpp','app.cpp','settings.cpp','platform_login.cpp','tests.cpp','glass_tests.cpp','main.cpp')) {
        $object = Join-Path $output ($source + '.o')
        $needsBuild = -not $sameOptions -or -not (Test-Path -LiteralPath $object)
        if (-not $needsBuild) {
            $built = (Get-Item -LiteralPath $object).LastWriteTimeUtc
            $needsBuild = (Get-Item -LiteralPath $source).LastWriteTimeUtc -gt $built -or @($headers | Where-Object {$_.LastWriteTimeUtc -gt $built}).Count -gt 0
        }
        if ($needsBuild) {
            & $Compiler @options '-c' $source '-o' $object
            if ($LASTEXITCODE -ne 0) { throw ('Native compilation failed: ' + $source) }
        }
        $objects += $object
    }
    $signature | Set-Content -LiteralPath $signatureFile -Encoding ASCII
    & $Compiler @options @objects (Join-Path $output 'app.res') '-o' (Join-Path $output 'CodexQuotaBar.exe') '-lgdiplus' '-lcomctl32' '-ldwmapi' '-lcomdlg32' '-lshell32' '-lshlwapi' '-lole32' '-ladvapi32' '-luuid' '-lgdi32' '-luser32' '-lwinhttp'
    if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
} finally { Pop-Location }
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $output -Force
Copy-Item -LiteralPath (Join-Path $root 'native/vendor/LICENSE-json.txt') -Destination (Join-Path $output 'LICENSE-json.txt') -Force
'{"version":"0.3.6","runtime":"native-win32","architecture":"x64"}' | Set-Content -LiteralPath (Join-Path $output 'version.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'native/USER_GUIDE.md') -Destination (Join-Path $output 'README.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'CHANGELOG.md') -Destination $output -Force
$manifestFiles = [ordered]@{}
foreach ($name in @('CodexQuotaBar.exe','version.json','LICENSE','LICENSE-json.txt','README.md','CHANGELOG.md')) {
    $manifestFiles[$name] = (Get-FileHash -LiteralPath (Join-Path $output $name)).Hash.ToLowerInvariant()
}
@{ product = 'CodexQuotaBar'; files = $manifestFiles } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'uninstall-manifest.json') -Encoding UTF8
Get-Item -LiteralPath (Join-Path $output 'CodexQuotaBar.exe') | Select-Object FullName,Length
