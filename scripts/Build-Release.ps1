# Builds both halves and packages the release zip:
#   dist\MW2-GTA-<version>.zip
#     Install-MW2GTA.bat / Uninstall-MW2GTA.bat
#     installer\  (PowerShell installer scripts)
#     files\      (MW2GTA.asi, iw4l-gta.exe, default gta-bridge-settings.cfg)
#     README.md, LICENSE, NOTICE
# Requires PLUGIN_SDK_DIR (see bridge\Build-ASI.cmd) and a Rust toolchain.
param(
    [Parameter(Mandatory)][string]$Version,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $root 'iw4l\target' }

if (-not $SkipBuild) {
    & (Join-Path $root 'bridge\Build-ASI.cmd')
    if ($LASTEXITCODE) { throw 'Native bridge build failed.' }
    Push-Location (Join-Path $root 'iw4l')
    try {
        # The profile the bridge was developed and measured with.
        $env:CARGO_TARGET_DIR = $target
        $env:CARGO_PROFILE_DEV_DEBUG = '0'
        $env:CARGO_PROFILE_DEV_OPT_LEVEL = '1'
        cargo build -p launcher
        if ($LASTEXITCODE) { throw 'Runtime build failed.' }
    } finally { Pop-Location }
}

$stage = Join-Path $root "dist\MW2-GTA-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $stage 'files'), (Join-Path $stage 'installer') | Out-Null
Copy-Item (Join-Path $root 'bridge\build\MW2GTA.asi') (Join-Path $stage 'files')
Copy-Item (Join-Path $target 'debug\iw4l.exe') (Join-Path $stage 'files\iw4l-gta.exe')
Copy-Item (Join-Path $root 'installer\gta-bridge-settings.cfg') (Join-Path $stage 'files')
Copy-Item (Join-Path $root 'installer\*.ps1') (Join-Path $stage 'installer')
Copy-Item (Join-Path $root 'installer\*.bat') $stage
foreach ($doc in 'README.md', 'LICENSE', 'NOTICE') { Copy-Item (Join-Path $root $doc) $stage }

$zip = Join-Path $root "dist\MW2-GTA-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Get-FileHash $zip | ForEach-Object { "SHA-256 $($_.Hash)  $zip" }
