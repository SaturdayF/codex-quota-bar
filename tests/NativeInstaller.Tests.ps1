param([string]$SetupExe, [string]$RuntimeExe)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (-not $SetupExe) { $SetupExe = Join-Path $project 'artifacts/releases/v0.3.2/CodexQuotaBar-v0.3.2-Setup.exe' }
if (-not $RuntimeExe) { $RuntimeExe = Join-Path $project 'artifacts/native-win-x64/CodexQuotaBar.exe' }
$temporaryRoot = [IO.Path]::GetFullPath((Join-Path $project 'codex_tmp'))
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ('native-install-' + [Guid]::NewGuid().ToString('N'))))
if ((Split-Path -Parent $fixture) -ne $temporaryRoot) { throw 'Unsafe fixture path' }
$target = [IO.Path]::GetFullPath((Join-Path $fixture '自定义 路径/CodexQuotaBar'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$oldProcesses = @(Get-Process CodexQuotaBar -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
try {
    $foreign = Join-Path $fixture 'existing unrelated folder'
    New-Item -ItemType Directory -Path $foreign -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $foreign 'keep.txt') -Value 'unrelated'
    $rejected = Start-Process -FilePath $SetupExe -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/ISOLATEDTEST=1',('/DIR="' + $foreign + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($rejected.ExitCode -eq 0 -or -not (Test-Path -LiteralPath (Join-Path $foreign 'keep.txt')) -or (Test-Path -LiteralPath (Join-Path $foreign 'CodexQuotaBar.exe'))) { throw 'Nonempty unrelated folder was not rejected' }
    Write-Output 'PASS installer rejects existing unrelated directory without overwriting files'
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/LANG=chinesesimp','/TASKS=desktopicon','/ISOLATEDTEST=1',('/DIR="' + $target + '"'),('/LOG="' + (Join-Path $fixture 'setup.log') + '"'))
    $process = Start-Process -FilePath $SetupExe -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw ('Installer failed: ' + $process.ExitCode) }
    foreach ($file in @('CodexQuotaBar.exe','version.json','LICENSE-json.txt','installer.ini','unins000.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $file))) { throw ('Missing installed file: ' + $file) }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $target 'CodexQuotaBar.exe')).Hash -ne (Get-FileHash -LiteralPath $RuntimeExe).Hash) { throw 'Installed payload mismatch' }
    $shortcutPath = Join-Path $target 'test-desktop/Codex Quota Bar.lnk'
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcutPath)
    if (-not (Test-Path -LiteralPath $shortcutPath) -or $shortcut.TargetPath -ne (Join-Path $target 'CodexQuotaBar.exe') -or $shortcut.Arguments -ne '--settings') { throw 'Invalid desktop shortcut' }
    if (Test-Path -LiteralPath (Join-Path $target 'runtime')) { throw 'Native install contains runtime' }
    Write-Output 'PASS C++ EXE installs runtime-free payload to custom Unicode/space path'
    $check = Start-Process -FilePath (Join-Path $target 'CodexQuotaBar.exe') -ArgumentList @('--self-test','--output',('"' + (Join-Path $fixture 'self-test.txt') + '"')) -RedirectStandardError (Join-Path $fixture 'self-test-error.txt') -WindowStyle Hidden -Wait -PassThru
    if ($check.ExitCode -ne 0) { throw 'Custom installed app self-test failed' }
    Get-Content -LiteralPath (Join-Path $fixture 'self-test.txt') | Select-Object -Last 1
    $extra = Join-Path $target 'user-extra.txt'; Set-Content -LiteralPath $extra -Value 'unrelated user file'
    $uninstall = Start-Process -FilePath (Join-Path $target 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/ISOLATEDTEST=1') -WindowStyle Hidden -Wait -PassThru
    if ($uninstall.ExitCode -ne 0) { throw 'Custom-path uninstaller failed' }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ((Test-Path -LiteralPath (Join-Path $target 'CodexQuotaBar.exe')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if ((Test-Path -LiteralPath (Join-Path $target 'CodexQuotaBar.exe')) -or (Test-Path -LiteralPath $shortcutPath) -or -not (Test-Path -LiteralPath $extra)) { throw 'Uninstall removed unrelated file or left payload' }
    Write-Output 'PASS native custom-path uninstall removes recorded files and preserves user extra file'
    foreach ($idToCheck in $oldProcesses) { if (-not (Get-Process -Id $idToCheck -ErrorAction SilentlyContinue)) { throw 'Isolated test stopped existing quota listener' } }
    Write-Output 'PASS isolated installer test did not stop existing user listener'
} finally {
    foreach ($file in @('setup.log','self-test.txt','self-test-error.txt')) {
        if (Test-Path -LiteralPath (Join-Path $fixture $file)) { Copy-Item -LiteralPath (Join-Path $fixture $file) -Destination (Join-Path $temporaryRoot ('native-installer-' + $file)) -Force }
    }
    if ((Split-Path -Parent $fixture) -eq $temporaryRoot -and (Test-Path -LiteralPath $fixture)) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
