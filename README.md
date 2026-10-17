# MW2-GTA (Beta)

> ⚠️ **WARNING: this mod is in beta and still contains bugs.** Expect the
> player to sometimes get stuck on geometry, camera or control glitches, and
> missing or unfinished features. Back up your GTA San Andreas saves before
> playing, and press F6 at any time to return to normal GTA controls.

> **Note:** this README was (mostly) AI generated (Human Tested). If something in the
> installation doesn't work, please let me know by
> [opening an issue](../../issues).

**Call of Duty: Modern Warfare 2 (2009) gameplay inside GTA San Andreas.**

MW2-GTA is a passthrough mod. Press F6 in GTA San Andreas and you play in
first person with MW2's own movement, weapons, animations, HUD and combat
sounds, in GTA's world: GTA's NPCs, police, vehicles, wanted level, health
and explosions all react to you.

It is not a recreation of MW2's controls. The mod runs a modified
[IW4L](https://github.com/vladtrc/iw4L), an open-source MW2 runtime that loads
your own MW2 installation, alongside GTA. A native GTA plugin (`MW2GTA.asi`)
connects the two:

```
GTA SA (x86) + MW2GTA.asi            IW4L runtime (x64)
  world, NPCs, police, health  <-->    MW2 movement, weapons, animations,
  vehicles, damage, effects             camera, HUD, combat audio
```

- MW2 sprint, jump, crouch, prone, ADS, recoil and bob, with collision against
  GTA's world.
- Bullets, melee and explosions hit GTA NPCs and vehicles with native blood,
  damage, impacts and police reactions.
- An F8 weapon menu with MW2's guns, attachments and equipment: claymore, C4,
  throwing knife, flash, concussion and smoke grenades.
- Javelin and Stinger lock onto GTA vehicles; Javelin also locks onto ground.
- MW2-style health regeneration and optional infinite ammo.
- The mod steps aside for cutscenes, menus and vehicles (press F near a car to
  get in) and resumes on foot.

> **Beta.** Expect bugs. Mission scripts are not specifically
> supported yet.

## Requirements

- **GTA San Andreas 1.0 US** (Steam / Rockstar Launcher copies must be
  downgraded to 1.0 US).
- **An ASI loader** in the GTA folder. The easiest way to get one is the MixMods
  [SA Essentials pack](https://www.mixmods.com.br/2019/06/sa-essentials-pack/)
  (Silent's ASI Loader, SilentPatch and more).
- **Call of Duty: Modern Warfare 2 (2009)**, installed, with its multiplayer data
  (`zone\<language>\common_mp.ff`).
- Windows 10/11 64-bit and a DirectX 12 capable GPU.

## Install

1. Download `MW2-GTA-<version>.zip` from [Releases](../../releases) and extract it
   anywhere.
2. Close GTA San Andreas.
3. Run **`Install-MW2GTA.bat`**. It:
   - finds GTA San Andreas (or asks you to pick the folder) and warns if
     `gta_sa.exe` is not 1.0 US;
   - checks for an ASI loader and, if none is found, offers to open the MixMods
     Essentials pack page;
   - finds Modern Warfare 2 in your Steam libraries (or asks you to pick it);
   - detects Grinch's CheatMenu and offers to move its freecam key from F6
     (MW2-GTA's toggle) to F11, keeping a backup of its settings;
   - copies `MW2GTA.asi` into GTA and the MW2 runtime into `<GTA>\MW2GTA`
     (or `%LOCALAPPDATA%\MW2GTA` when GTA is under Program Files);
   - optionally creates a desktop shortcut.

The installer **never deletes or changes save games** (GTA's are in
`Documents\GTA San Andreas User Files`; MW2's folder is only read). Files it
replaces are backed up to `<GTA>\MW2GTA-backup\<date>`.

To remove the mod, run **`Uninstall-MW2GTA.bat`**.

## Play

Start GTA San Andreas, load your game, and press **F6**. The first start
prepares MW2 assets and takes about a minute.

| Key | Action |
|---|---|
| F6 | MW2 mode on/off |
| WASD, Shift, Space | Move, sprint (hold Shift while aiming to steady a scope), jump |
| C / Ctrl | Crouch / prone |
| Left / right mouse | Fire / aim (with akimbo: right gun / left gun) |
| R | Reload |
| 1 / 2, mouse wheel | Previous / next weapon |
| V | Melee |
| E | Use (e.g. pick up a thrown knife) |
| G / H | Lethal equipment / tactical equipment |
| F | Get into a nearby vehicle (MW2 mode returns when you get out) |
| F8 | Weapon and equipment menu (arrows to choose, Enter to equip) |
| F9 | Refill ammo |
| F7 | Options: FOV, help text, infinite ammo, health regeneration, peaceful world, invert mouse, MW2 resolution |

**MW2 resolution** (F7): "Match GTA" renders MW2 at GTA's aspect ratio (up to
1280x720); fixed sizes are also available. Changing it restarts the MW2 runtime.

## Known limitations

- GTA missions are not specifically supported: missions that require a
  particular GTA weapon or GTA-only actions (climbing, swimming) may not work in
  MW2 mode. Press F6 to switch back to GTA controls at any time.
- Water, ladders and moving platforms are not adapted to MW2 movement.
- Flashbangs make GTA NPCs freeze in place; GTA has no blinded state.
- Killstreaks, MW2 radar contacts and scoreboard are not integrated with GTA.
- Occasional short MW2 hitches can still happen.
- C4 places two charges before the detonator comes up; it should come up
  after one.
- With the Javelin, part of the arm (the sleeve) is drawn stretched in first
  person.
- Tanks have no collision with the player for now: you can walk through them.

## Troubleshooting

- **The camera jumps to a frozen spot when entering a vehicle (or "player
  teleported" appears).** Grinch's CheatMenu also uses F6, for its freecam,
  and Enter teleports the player while that freecam is on. Let the installer
  move the freecam key, or in CheatMenu open Menu > Hotkeys and rebind
  "Toggle freecam" to another key.
- **Nothing happens on F6.** Check that an ASI loader is installed and that
  `MW2GTA.log` appears in the GTA folder after starting the game.

## Building from source

The repository has two halves:

| Path | What |
|---|---|
| `bridge/` | Native GTA SA plugin (`MW2GTA.asi`), C++ against plugin-sdk |
| `iw4l/` | Modified IW4L runtime (Rust) |
| `installer/` | Release installer and uninstaller |

Prerequisites: Visual Studio Build Tools with C++ (x86), a built
[plugin-sdk](https://github.com/DK22Pac/plugin-sdk) for GTA SA, and Rust
(rustup; the toolchain is pinned by `iw4l/rust-toolchain.toml`).

```powershell
$env:PLUGIN_SDK_DIR = 'C:\dev\plugin-sdk'
bridge\Build-ASI.cmd                     # -> bridge\build\MW2GTA.asi
cd iw4l
$env:CARGO_PROFILE_DEV_DEBUG = '0'; $env:CARGO_PROFILE_DEV_OPT_LEVEL = '1'
cargo build -p launcher                  # -> iw4l\target\debug\iw4l.exe
```

Install `MW2GTA.asi` into the GTA folder, and `iw4l.exe` (renamed
`iw4l-gta.exe`) with `installer\gta-bridge-settings.cfg` into `<GTA>\MW2GTA`.
The native and runtime halves share a memory layout (`bridge/src/Protocol.h` and `iw4l/crates/bootstrap/src/gta_bridge.rs`)
and must always be built and shipped together.

## Credits

- **[IW4L](https://github.com/vladtrc/iw4L)** by vladtrc: the open-source MW2
  runtime this mod is built on (Apache-2.0). See `iw4l/NOTICE` for IW4L's own
  credits and third-party licences.
- **[plugin-sdk](https://github.com/DK22Pac/plugin-sdk)** by DK22Pac and
  contributors: the GTA SA plugin SDK used by the native bridge (MIT).
- **[MixMods](https://www.mixmods.com.br/)** (Junior_Djjr) for the
  [SA Essentials pack](https://www.mixmods.com.br/2019/06/sa-essentials-pack/)
  recommended by the installer, and to the authors of the ASI loaders and
  SilentPatch it contains.
- MW2-GTA bridge, installer and IW4L changes: **Ermes_x86 (Payne)**.

## License

Apache License 2.0; see [LICENSE](LICENSE) and [NOTICE](NOTICE).

No game data is included. You need your own copies of GTA San Andreas and
Call of Duty: Modern Warfare 2. This project is not affiliated with Activision,
Infinity Ward, Rockstar Games or Take-Two Interactive.
