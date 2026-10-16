# MW2-GTA uninstaller. Removes only files installed by MW2-GTA. Game saves,
# the MW2 installation and other GTA mods are never touched.
param(
    [string]$GtaDir,
    # Unattended: accept every default answer.
    [switch]$Yes
)
$ErrorActionPreference = 'Stop'

function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }
function Ask-YesNo([string]$question, [bool]$default = $true) {
    $hint = if ($default) { '[Y/n]' } else { '[y/N]' }
    if ($Yes) { Say "$question $hint -> $(if ($default) { 'yes' } else { 'no' })"; return $default }
    while ($true) {
        $answer = (Read-Host "$question $hint").Trim().ToLowerInvariant()
        if ($answer -eq '') { return $default }
        if ($answer -in 'y', 'yes', 's', 'sim') { return $true }
        if ($answer -in 'n', 'no', 'nao', 'não') { return $false }
    }
}

Say 'MW2-GTA uninstaller' Cyan
foreach ($name in 'gta_sa', 'iw4l-gta') {
    while (Get-Process -Name $name -ErrorAction SilentlyContinue) {
        Say "$name.exe is running. Close the game, then press Enter." Yellow
        if ($Yes) { throw "$name.exe is running; close it before uninstalling." }
        Read-Host | Out-Null
    }
}
while (-not ($GtaDir -and (Test-Path (Join-Path $GtaDir 'MW2GTA.install.json')))) {
    if ($Yes) { throw 'MW2-GTA install not found; pass -GtaDir.' }
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the GTA San Andreas folder where MW2-GTA is installed'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'Uninstall cancelled.' }
    $GtaDir = $dialog.SelectedPath
    if (-not (Test-Path (Join-Path $GtaDir 'MW2GTA.install.json'))) { Say 'MW2-GTA is not installed in that folder.' Red }
}
$manifest = Get-Content (Join-Path $GtaDir 'MW2GTA.install.json') -Raw | ConvertFrom-Json

foreach ($file in 'MW2GTA.asi', 'MW2GTA.log', 'MW2GTA.capture.bmp') {
    $path = Join-Path $GtaDir $file
    if (Test-Path $path) { Remove-Item $path -Force; Say "Removed $file" }
}
$ini = Join-Path $GtaDir 'MW2GTA.ini'
if ((Test-Path $ini) -and (Ask-YesNo 'Remove your MW2-GTA options (MW2GTA.ini)?')) { Remove-Item $ini -Force; Say 'Removed MW2GTA.ini' }

$runtime = $manifest.runtime
if ($runtime -and (Test-Path (Join-Path $runtime 'iw4l-gta.exe'))) {
    Say "The MW2 runtime folder is $runtime" Yellow
    Say '(it holds the runtime, its caches/logs and its own MW2 unlock progress - not your MW2 or GTA saves).'
    if (Ask-YesNo 'Delete the MW2 runtime folder?') {
        Remove-Item $runtime -Recurse -Force
        Say 'Removed the MW2 runtime folder.'
    }
}
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) 'GTA San Andreas (MW2-GTA).lnk'
if (Test-Path $desktop) { Remove-Item $desktop -Force; Say 'Removed the desktop shortcut.' }
Remove-Item (Join-Path $GtaDir 'MW2GTA.install.json') -Force
Say ''
Say 'MW2-GTA uninstalled. Your GTA SA and MW2 saves were not touched.' Green
if ($manifest.backup -and (Test-Path $manifest.backup)) { Say "Earlier backups remain in: $($manifest.backup)" }
