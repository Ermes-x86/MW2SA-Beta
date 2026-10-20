//! MW2 announcer lines for GTA events. GTA chooses the line (police UAV,
//! helicopter, escaping, killstreaks...) and the runtime plays it in the
//! map's announcer voice, as MW2's `playLocalSound` would.
use super::*;

/// Line ids shared with `bridge/src/GameplayHost.h` (`Announce`).
const LINES: &[&str] = &[
    "",
    "enemy_uav",
    "enemy_heli",
    "mission_success",
    "mission_fail",
    "achieve_uav",
    "use_uav",
    "achieve_carepackage",
    "use_carepackage",
    "achieve_predator",
    "use_predator",
    "enemy_pavelow",
    "enemy_airstrike",
];
#[derive(Default)]
pub(super) struct State {
    /// Sequence and line id written by GTA, and its announcer volume.
    pub request: (u32, u32),
    pub volume: f32,
    played: u32,
    voice: Option<String>,
}
pub(super) fn play(
    mut bridge: ResMut<Bridge>,
    catalog: Option<Res<asset_game::MenuCatalog>>,
    bank: Option<Res<audio::SoundBank>>,
    identity: Option<Res<frame::LaunchIdentity>>,
    mut commands: MessageWriter<audio::AliasCommand>,
) {
    let (sequence, line) = bridge.announcer.request;
    if sequence == bridge.announcer.played {
        return;
    }
    let (Some(bank), Some(identity)) = (bank, identity) else {
        return;
    };
    bridge.announcer.played = sequence;
    if bridge.announcer.voice.is_none() {
        let (allies, axis) = audio::voice_prefixes_for_zone(catalog.as_deref(), Some(&bank), &identity);
        bridge.announcer.voice = allies.or(axis);
    }
    let (Some(voice), Some(name)) = (&bridge.announcer.voice, LINES.get(line as usize)) else {
        return;
    };
    if name.is_empty() {
        return;
    }
    let alias = format!("{voice}1mc_{name}");
    diag::info!(Launch, "GTA announcer: {alias}");
    let volume = bridge.announcer.volume;
    commands.write(audio::AliasCommand::PlayScaled {
        sound: audio::PlayAlias {
            event: None,
            namespace: asset_core::AssetNamespace::Iw4,
            alias,
            fallback: None,
            origin_inches: None,
            snd_ent: Some(audio::SND_ENT_LOCAL),
        },
        volume: if volume.is_finite() && volume > 0.0 { volume.min(4.0) } else { 1.0 },
    });
}
