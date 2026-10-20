# MW2-GTA (Beta)

> ⚠️ **WARNING: this mod is in beta and still contains bugs.** Expect the
> player to sometimes get stuck on geometry, camera or control glitches, and
> missing or unfinished features. Back up your GTA San Andreas saves before
> playing, and press F6 at any time to return to normal GTA controls.

> ⚖️ **YOU NEED TO OWN BOTH GAMES TO USE THIS MOD.** You need your own legal
> copies of **Grand Theft Auto: San Andreas** and **Call of Duty: Modern
> Warfare 2 (2009)**. This repository and its releases **do not contain any
> game files**: no maps, models, textures, sounds, videos or executables
> from either game. The mod only reads the games already
> installed on your PC. Do not ask for game files here, and do not use
> pirated copies. See [Legal notice](#legal-notice).

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
- GTA takes over automatically while CJ swims, uses a jetpack, carries an
  object or skydives with a parachute, and when you jump (Space) at a fence,
  wall or ledge, so CJ climbs or vaults it. MW2 comes back right after.
- Only GTA damage counts: the MW2 player never dies or respawns. Falls while
  in MW2 mode hurt CJ as in GTA.
- Your loadout (guns, attachments, equipment) is remembered between sessions.
- GTA weapons become MW2 guns: buy one at Ammu-Nation or get one from a cheat
  or mission and you get the matching MW2 gun; guns on the ground are picked
  up with E.
- MW2 killstreaks for GTA kills, once per life as in MW2: UAV at 6 kills (a
  plane circles overhead and every enemy shows on the radar for 30 s), Care
  Package at 8 (a supply drop where you aim) and Predator Missile at 10 (steer
  a missile from the sky).
- The MW2 announcer reacts to the police (enemy UAV, enemy chopper, mission
  success or failure), with its own volume.
- MW2-style radar: enemies who fire an unsilenced gun appear on GTA's radar.
- Flashbangs blind you and make GTA NPCs cough and cover their eyes; health
  and armour are shown as rings around the radar.

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

**Recommended** for smooth play at the best quality: at least a 4-core CPU,
16 GB of RAM and a GPU with 2 GB of VRAM. GTA and MW2 run at the same time,
so the PC needs room for both.

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

The mod only reads Modern Warfare 2. Other Call of Duty games installed on the
PC (MW3, Black Ops, Black Ops II) are ignored.

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
| E (at a gun on the ground) | Pick up the gun (you get the matching MW2 gun) |
| 4 / 5 / 6 | Use a killstreak: UAV / Care Package / Predator Missile |
| F8 | Weapon and equipment menu (arrows to choose, Enter to equip) |
| F9 | Refill ammo |
| F7 | Options: FOV, announcer volume, text size, help text, infinite ammo, health regeneration, peaceful world, invert mouse, announcer on/off, MW2 resolution |

**MW2 resolution** (F7): "Match GTA" renders MW2 at GTA's aspect ratio (up to
1280x720); fixed sizes are also available. Changing it restarts the MW2 runtime.

## Known limitations

- GTA missions are not tested or supported in MW2 mode. Missions that need a
  particular GTA item (camera, spray can), stealth or noise meters may not
  work. Press F6 to switch back to GTA controls at any time.
- Ladders and moving platforms (standing on a moving train or truck) are not
  adapted to MW2 movement.
- Entering stores and other interiors (shops, Ammu-Nation, restaurants) does
  not work yet in MW2 mode. Press F6 to switch to GTA controls to go inside.
- Only three killstreaks (UAV, Care Package, Predator Missile) exist so far,
  and the MW2 scoreboard is not integrated with GTA.
- Picking up ammo for an MW2 gun you already own does not refill it yet.
- While steering the Predator Missile, CJ stands still and can be attacked.
- Occasional short MW2 hitches can still happen.
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

Apache License 2.0; see [LICENSE](LICENSE) and [NOTICE](NOTICE). The
licence covers this project's own code only, not the games it works with.

## Legal notice

- **You must own both games.** MW2-GTA works only with your own legally
  obtained copies of Grand Theft Auto: San Andreas and Call of Duty: Modern
  Warfare 2 (2009), installed on your PC.
- **No game content is distributed.** This repository and its releases
  contain only the mod's own source code and the programs built from it
  (`MW2GTA.asi`, `iw4l-gta.exe`, the installer). They contain no game
  files or assets from either game. Game content is read at run time from
  your own installations and stays on your PC.
- **Do not share game files.** Issues, pull requests or comments that
  contain or link to game files, cracks or pirated copies will be removed.
- **Unofficial fan project.** MW2-GTA is a free, non-commercial fan mod. It
  is not affiliated with, endorsed by or sponsored by Activision, Infinity
  Ward, Rockstar Games or Take-Two Interactive.
- **Trademarks.** Grand Theft Auto, San Andreas, Call of Duty and Modern
  Warfare are trademarks of their respective owners and are used here only
  to describe which games the mod works with.
- **Use at your own risk.** The mod is provided "as is", without warranty
  (see the [LICENSE](LICENSE)). Back up your saves before installing mods.
- **Rights holders:** if you have a concern about this project, please
  [open an issue](../../issues) and it will be addressed promptly.
