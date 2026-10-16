# MW2-GTA installer. Installs the ASI bridge into GTA San Andreas and the MW2
# runtime next to it, pointing the runtime at an existing MW2 installation.
#
# It never deletes or modifies save games:
#   - GTA SA saves live in Documents\GTA San Andreas User Files (never touched).
#   - The MW2 folder is only read; a shortcut to it is created in the runtime.
# Files this installer would replace are backed up first.
param(
    [string]$GtaDir,
    [string]$Mw2Dir,
    [string]$RuntimeDir,
    # Unattended: accept every default answer (folders must be found or given).
    [switch]$Yes,
    [switch]$NoDesktopShortcut
)
$ErrorActionPreference = 'Stop'
$package = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$files = Join-Path $package 'files'
$essentialsUrl = 'https://www.mixmods.com.br/2019/06/sa-essentials-pack/'

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
function Pick-Folder([string]$description, [string]$start) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = $description
    $dialog.ShowNewFolderButton = $false
    if ($start -and (Test-Path $start)) { $dialog.SelectedPath = $start }
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.SelectedPath }
    return $null
}
function Test-Gta([string]$dir) { $dir -and (Test-Path (Join-Path $dir 'gta_sa.exe')) }
function Test-Mw2([string]$dir) {
    if (-not $dir) { return $false }
    $zone = Join-Path $dir 'zone'
    if (-not (Test-Path $zone)) { return $false }
    return [bool](Get-ChildItem -Path $zone -Filter 'common_mp.ff' -Recurse -Depth 1 -ErrorAction SilentlyContinue | Select-Object -First 1)
}
function Steam-Libraries {
    $roots = @()
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $item = Get-ItemProperty $key -ErrorAction Stop
            foreach ($name in 'SteamPath', 'InstallPath') { if ($item.$name) { $roots += ($item.$name -replace '/', '\') } }
        } catch {}
    }
    $libraries = @()
    foreach ($root in ($roots | Select-Object -Unique)) {
        $libraries += $root
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($match in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libraries += ($match.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    return $libraries | Select-Object -Unique
}
function Backup-File([string]$path, [string]$backupDir, [string]$replacement) {
    if (Test-Path $path) {
        # Nothing to preserve when the installed file is already identical.
        if ($replacement -and (Get-FileHash $path).Hash -eq (Get-FileHash $replacement).Hash) { return }
        New-Item -ItemType Directory -Force $backupDir | Out-Null
        Copy-Item $path (Join-Path $backupDir (Split-Path -Leaf $path)) -Force
    }
}
function Set-IniValue([string[]]$lines, [string]$key, [string]$value) {
    $found = $false
    $result = foreach ($line in $lines) {
        if ($line -match "^\s*$([regex]::Escape($key))\s*=") { $found = $true; "$key=$value" } else { $line }
    }
    if (-not $found) { $result = @($result) + "$key=$value" }
    return , @($result)
}

Clear-Host
Say '=============================================================' Cyan
Say '  MW2-GTA (Beta)  -  MW2 gameplay inside GTA San Andreas' Cyan
Say '=============================================================' Cyan
Say ''
Say 'This installer will:'
Say '  1. Find your GTA San Andreas folder and check its ASI loader.'
Say '  2. Find your Call of Duty: Modern Warfare 2 (2009) folder.'
Say '  3. Copy MW2GTA.asi into GTA and the MW2 runtime next to it.'
Say ''
Say 'Your GTA SA and MW2 save games are never deleted or changed.' Green
Say '(But a backup is always recommended when installing mods.)' Yellow
Say ''

foreach ($name in 'gta_sa', 'iw4l-gta') {
    while (Get-Process -Name $name -ErrorAction SilentlyContinue) {
        Say "$name.exe is running. Close the game, then press Enter." Yellow
        if ($Yes) { throw "$name.exe is running; close it before installing." }
        Read-Host | Out-Null
    }
}
foreach ($required in 'MW2GTA.asi', 'iw4l-gta.exe', 'gta-bridge-settings.cfg') {
    if (-not (Test-Path (Join-Path $files $required))) {
        throw "The package is incomplete: files\$required is missing. Download the full release zip again."
    }
}

# --- GTA San Andreas -------------------------------------------------------
if (-not (Test-Gta $GtaDir)) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Rockstar Games\GTA San Andreas",
        "$env:ProgramFiles\Rockstar Games\GTA San Andreas",
        'C:\Games\GTA San Andreas', 'D:\Games\GTA San Andreas', 'C:\GTA San Andreas', 'D:\GTA San Andreas'
    ) + (Steam-Libraries | ForEach-Object { Join-Path $_ 'steamapps\common\Grand Theft Auto San Andreas' })
    $GtaDir = $candidates | Where-Object { Test-Gta $_ } | Select-Object -First 1
    if ($GtaDir) {
        Say "Found GTA San Andreas: $GtaDir"
        if (-not (Ask-YesNo 'Use this folder?')) { $GtaDir = $null }
    }
    while (-not (Test-Gta $GtaDir)) {
        if ($Yes) { throw 'GTA San Andreas folder not found; pass -GtaDir.' }
        Say 'Select your GTA San Andreas folder (the one containing gta_sa.exe).' Yellow
        $GtaDir = Pick-Folder 'Select the GTA San Andreas folder (contains gta_sa.exe)' $GtaDir
        if (-not $GtaDir) { throw 'Installation cancelled: no GTA San Andreas folder selected.' }
        if (-not (Test-Gta $GtaDir)) { Say 'That folder does not contain gta_sa.exe.' Red }
    }
}
$exe = Get-Item (Join-Path $GtaDir 'gta_sa.exe')
# Known 1.0 US executables: original (14,383,616) and the popular compact exe (5,189,632).
if ($exe.Length -notin 14383616, 5189632) {
    Say ''
    Say 'WARNING: gta_sa.exe does not look like version 1.0 US.' Yellow
    Say 'MW2-GTA only runs on GTA SA 1.0 US. Steam / Rockstar Launcher copies must be' Yellow
    Say 'downgraded first (the MixMods Essentials Pack page explains how).' Yellow
    if (-not (Ask-YesNo 'Continue anyway?' $false)) { throw 'Installation cancelled: GTA SA 1.0 US required.' }
}

$asiLoader = @('vorbisHooked.dll', 'dinput8.dll') | Where-Object { Test-Path (Join-Path $GtaDir $_) }
if (-not $asiLoader) {
    Say ''
    Say 'No ASI loader was found in your GTA San Andreas folder.' Yellow
    Say 'MW2-GTA is an .asi plugin and needs an ASI loader to run.' Yellow
    Say 'Recommended: the MixMods "SA Essentials" pack (ASI Loader, SilentPatch, etc.):' Yellow
    Say "  $essentialsUrl" Cyan
    if (-not $Yes -and (Ask-YesNo 'Open that page in your browser now?')) { Start-Process $essentialsUrl }
    if (-not (Ask-YesNo 'Continue installing MW2-GTA now (install the ASI loader afterwards)?')) {
        throw 'Installation paused: install an ASI loader, then run this installer again.'
    }
} else {
    Say "ASI loader found ($($asiLoader -join ', '))." Green
}

# --- Grinch's CheatMenu ----------------------------------------------------
# CheatMenu toggles its freecam with F6 by default, the same key as MW2-GTA.
# Its freecam then stays on unseen behind MW2 mode and appears (frozen where
# F6 was pressed) whenever MW2 hands control back, e.g. when entering a car.
# Its Enter key also teleports the player while that freecam is on.
foreach ($asi in @(Get-ChildItem -Path $GtaDir -Filter 'CheatMenuSA.asi' -Recurse -Depth 4 -File -ErrorAction SilentlyContinue)) {
    $config = Join-Path $asi.DirectoryName 'CheatMenuSA.toml'
    $text = if (Test-Path $config) { [IO.File]::ReadAllText($config) } else { '' }
    $section = [regex]'(?m)^\[Hotkeys\.Freecam\.Toggle\]\s*\r?\nKey1\s*=\s*(\d+)\s*\r?\nKey2\s*=\s*(\d+)'
    $match = $section.Match($text)
    if ($match.Success -and $match.Groups[1].Value -ne '117' -and $match.Groups[2].Value -ne '117') { continue }
    Say ''
    Say "Grinch's CheatMenu found: $($asi.DirectoryName)" Yellow
    Say 'Its freecam is toggled with F6, the same key as MW2-GTA. That makes the camera' Yellow
    Say 'jump to a free camera when MW2 mode hands control back (for example in vehicles).' Yellow
    if (Ask-YesNo 'Move the CheatMenu freecam key to F11 (a backup of its settings is kept)?') {
        if (Test-Path $config) { Copy-Item $config "$config.before-mw2gta.bak" -Force }
        $replacement = "[Hotkeys.Freecam.Toggle]`r`nKey1 = 122`r`nKey2 = 122"
        if ($match.Success) { $text = $section.Replace($text, $replacement, 1) }
        else { $text = $text.TrimEnd() + $(if ($text) { "`r`n`r`n" } else { '' }) + $replacement + "`r`n" }
        [IO.File]::WriteAllText($config, $text, (New-Object Text.UTF8Encoding $false))
        Say 'CheatMenu freecam now uses F11.' Green
    } else {
        Say 'Left unchanged. Rebind "Toggle freecam" in CheatMenu > Menu > Hotkeys if the camera jumps.' Yellow
    }
}

# --- Modern Warfare 2 ------------------------------------------------------
if (-not (Test-Mw2 $Mw2Dir)) {
    $Mw2Dir = Steam-Libraries | ForEach-Object { Join-Path $_ 'steamapps\common\Call of Duty Modern Warfare 2' } |
        Where-Object { Test-Mw2 $_ } | Select-Object -First 1
    if ($Mw2Dir) {
        Say "Found Modern Warfare 2: $Mw2Dir"
        if (-not (Ask-YesNo 'Use this folder?')) { $Mw2Dir = $null }
    }
    while (-not (Test-Mw2 $Mw2Dir)) {
        if ($Yes) { throw 'Modern Warfare 2 folder not found; pass -Mw2Dir.' }
        Say 'Select your Call of Duty: Modern Warfare 2 (2009) folder (contains the "zone" folder).' Yellow
        $Mw2Dir = Pick-Folder 'Select the Modern Warfare 2 (2009) folder (contains zone\...\common_mp.ff)' $Mw2Dir
        if (-not $Mw2Dir) { throw 'Installation cancelled: no Modern Warfare 2 folder selected.' }
        if (-not (Test-Mw2 $Mw2Dir)) { Say 'That folder has no zone\<language>\common_mp.ff (MW2 multiplayer data).' Red }
    }
}

# --- Runtime location ------------------------------------------------------
if (-not $RuntimeDir) {
    $RuntimeDir = Join-Path $GtaDir 'MW2GTA'
    # The runtime writes caches and logs; Program Files is not writable for it.
    if ($GtaDir -like "$env:ProgramFiles*" -or $GtaDir -like "${env:ProgramFiles(x86)}*") {
        $RuntimeDir = Join-Path $env:LOCALAPPDATA 'MW2GTA'
    }
}
Say ''
Say 'Ready to install:' Cyan
Say "  GTA San Andreas : $GtaDir"
Say "  Modern Warfare 2: $Mw2Dir"
Say "  MW2 runtime     : $RuntimeDir"
if (-not (Ask-YesNo 'Install now?')) { throw 'Installation cancelled.' }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $GtaDir "MW2GTA-backup\$stamp"
New-Item -ItemType Directory -Force $RuntimeDir | Out-Null

# Bridge plugin.
Backup-File (Join-Path $GtaDir 'MW2GTA.asi') $backup (Join-Path $files 'MW2GTA.asi')
Copy-Item (Join-Path $files 'MW2GTA.asi') (Join-Path $GtaDir 'MW2GTA.asi') -Force

# Runtime. Player settings and the runtime account (progression) are kept.
Backup-File (Join-Path $RuntimeDir 'iw4l-gta.exe') $backup (Join-Path $files 'iw4l-gta.exe')
Copy-Item (Join-Path $files 'iw4l-gta.exe') (Join-Path $RuntimeDir 'iw4l-gta.exe') -Force
$settings = Join-Path $RuntimeDir 'gta-bridge-settings.cfg'
if (-not (Test-Path $settings)) { Copy-Item (Join-Path $files 'gta-bridge-settings.cfg') $settings }

# Shortcut the runtime uses to read the MW2 installation (read-only).
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path $RuntimeDir 'Modern Warfare 2.lnk'))
$link.TargetPath = $Mw2Dir
$link.Save()

# Bridge settings: keep the player's options, update only the runtime location.
$ini = Join-Path $GtaDir 'MW2GTA.ini'
if (Test-Path $ini) {
    Backup-File $ini $backup
    $lines = @(Get-Content $ini)
} else {
    $lines = @('[Bridge]', 'FOV=96.0', 'EyeOffset=0.72', 'Sensitivity=0.13', 'SelfTest=0',
        'HealthRegen=1', 'InfiniteAmmo=0', 'Resolution=2')
}
$lines = Set-IniValue $lines 'Runtime' $RuntimeDir
$lines = Set-IniValue $lines 'RuntimeExecutable' 'iw4l-gta.exe'
$lines = Set-IniValue $lines 'MappingName' 'MW2GTA_v1'
Set-Content -Path $ini -Value $lines -Encoding Unicode

# Record what was installed so the uninstaller removes only these files.
$manifest = [ordered]@{
    installed = (Get-Date).ToString('s')
    gta = $GtaDir
    mw2 = $Mw2Dir
    runtime = $RuntimeDir
    backup = $(if (Test-Path $backup) { $backup } else { $null })
}
$manifest | ConvertTo-Json | Set-Content -Path (Join-Path $GtaDir 'MW2GTA.install.json') -Encoding UTF8

if (-not $NoDesktopShortcut -and (Ask-YesNo 'Create a desktop shortcut "GTA San Andreas (MW2-GTA)"?')) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    $play = $shell.CreateShortcut((Join-Path $desktop 'GTA San Andreas (MW2-GTA).lnk'))
    $play.TargetPath = Join-Path $GtaDir 'gta_sa.exe'
    $play.WorkingDirectory = $GtaDir
    $play.Save()
}

Say ''
Say 'MW2-GTA installed.' Green
if (Test-Path $backup) { Say "Replaced files were backed up to: $backup" }
Say ''
Say 'How to play:' Cyan
Say '  Start GTA San Andreas, load your game, then press F6. The first start'
Say '  prepares MW2 assets and can take about a minute.'
Say '  F6 on/off  F7 options  F8 weapons  F9 ammo  V melee  E use  G/H equipment'
if (-not $asiLoader) { Say 'Remember to install an ASI loader first (see the link above).' Yellow }
