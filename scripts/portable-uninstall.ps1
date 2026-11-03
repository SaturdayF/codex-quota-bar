param([int]$ParentId = 0, [Parameter(Mandatory)][string]$InstallDirectory, [switch]$IsolatedTest)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
if ($target -eq [IO.Path]::GetPathRoot($target).TrimEnd('\')) { throw 'Unsafe portable directory' }
$marker = Join-Path $target 'portable.json'
$identity = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
if ($identity.product -ne 'CodexQuotaBar' -or $identity.version -ne '0.3.1') { throw 'Invalid portable identity' }
$binary = Join-Path $target 'CodexQuotaBar.exe'
if ((Get-Item -LiteralPath $binary).VersionInfo.FileVersion -ne $identity.version) { throw 'Portable version mismatch' }
if (Test-Path -LiteralPath (Join-Path $target 'installer.ini')) { throw 'Use the installed uninstaller' }
if ($ParentId -gt 0) { Wait-Process -Id $ParentId -ErrorAction SilentlyContinue }
# Never recursively remove a portable directory: user files may live beside it.
foreach ($name in @('CodexQuotaBar.exe','version.json','LICENSE','LICENSE-json.txt','README.md','CHANGELOG.md','portable.json','portable-uninstall.ps1')) {
    $file = Join-Path $target $name
    if (Test-Path -LiteralPath $file -PathType Leaf) { Remove-Item -LiteralPath $file -Force }
}
if (-not @(Get-ChildItem -LiteralPath $target -Force).Count) { Remove-Item -LiteralPath $target }
