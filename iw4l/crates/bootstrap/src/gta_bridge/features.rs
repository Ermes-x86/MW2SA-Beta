use super::*;

pub(super) const BASE: usize = TRIANGLES + MAX_TRIANGLES * 40;
const CATALOG: usize = BASE + 64;
const PROJECTILES: usize = CATALOG + 128 * 64;
const EXPLOSIONS: usize = PROJECTILES + 32 * 48;
pub(super) const END: usize = EXPLOSIONS + 16 * 48;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub(super) struct Request {
    sequence: u32,
    kind: u32,
    weapon: u32,
}
#[derive(Default)]
pub(super) struct State {
    pub request: Request,
    received: u32,
    pending: Option<(u32, u32)>,
    ack: u32,
    outcome: u32,
    catalog: Vec<Entry>,
    event: u32,
    explosions: u32,
    equipment_epoch: u32,
    forced_selection: Option<u32>,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Entry {
    weapon: u32,
    kind: u32,
    name: [u8; 56],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Projectile {
    id: u32,
    weapon: u32,
    origin: [f32; 3],
    angles: [f32; 3],
    kind: u32,
    tick: u32,
    reserved: [u32; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Explosion {
    sequence: u32,
    id: u32,
    weapon: u32,
    kind: u32,
    origin: [f32; 3],
    radius: f32,
    inner_damage: f32,
    outer_damage: f32,
    tick: u32,
    reserved: u32,
}
const _: () = assert!(
    size_of::<Entry>() == 64 && size_of::<Projectile>() == 48 && size_of::<Explosion>() == 48
);

/// Killstreak, vehicle, scripted-object and placeholder weapons. They have no
/// first-person model or behaviour a player can use from the selector.
const NOT_HANDHELD: &[&str] = &[
    "killstreak_",
    "destructible_",
    "ac130_",
    "airdrop_",
    "artillery_",
    "barrel_",
    "briefcase_bomb",
    "cobra_",
    "defaultweapon",
    "flare_",
    "gl_mp",
    "harrier_",
    "heli_",
    "lightstick_",
    "littlebird_",
    "nuke_",
    "none_",
    "onemanarmy_",
    "pavelow_",
    "remotemissile_",
    "semtex_",
    "sentry_",
    "stealth_bomb_",
    "uav_",
];
pub(super) fn host_actions(
    mut bridge: ResMut<Bridge>,
    world: Option<ResMut<net::AuthorityWorld>>,
    local: Res<net::LocalPresentClient>,
    mut inbox: ResMut<net::ClientActionInbox>,
    mut ids: ResMut<net::ActionRequestIds>,
    select: Option<ResMut<net::WeaponSelect>>,
    clock: Option<Res<net::FrameClock>>,
) {
    let Some(mut world) = world else { return };
    if !host_has_player(&bridge) || world.0.player(local.0).is_none_or(|ps| ps.pm_type != 0) {
        return;
    }
    if let (Some(weapon), Some(mut select), Some(clock)) =
        (bridge.features.forced_selection.take(), select, clock)
    {
        // A give can replace the previously owned gun. Do not let an old
        // client selection request immediately select the removed weapon.
        select.index = weapon;
        select.mapped_index = weapon;
        select.last_primary = 0;
        select.time = clock.time();
    }
    // Make the grenade control consistently available with the real offhand
    // inventory; giving equipment does not replace the gun currently in hand.
    if bridge.features.equipment_epoch != bridge.host_movement.epoch {
        bridge.features.equipment_epoch = bridge.host_movement.epoch;
        if let Some(weapon) = world.0.weapon_index_by_script_name("frag_grenade_mp") {
            let request_id = ids.allocate();
            let _ = inbox.push(
                local.0,
                sim::ClientAction::GiveWeapon {
                    request_id,
                    weapon,
                    model: 0,
                },
            );
        }
    }
    // MW2 raises the C4 detonator only once no charge is left in hand, which
    // with its two charges needs two throws. Keep one charge at a time so a
    // single thrown C4 can be detonated; resupply refills it after that.
    let c4 = bridge.features.catalog.iter().find(|e| e.name.starts_with(b"c4\0")).map(|e| e.weapon);
    if let Some(c4) = c4
        && let Some((clip, stock)) = world.0.cap_ammo(local.0, c4, 1)
    {
        diag::info!(Launch, "GTA C4 limited to one charge: had clip={clip} reserve={stock}");
    }
    let request = bridge.features.request;
    if request.sequence == 0 || request.sequence == bridge.features.received {
        return;
    }
    bridge.features.received = request.sequence;
    if request.kind == 3 {
        // `weapon` carries the id of a planted projectile GTA saw triggered.
        let applied = world.0.detonate_projectile(local.0, request.weapon);
        bridge.features.ack = request.sequence;
        bridge.features.outcome = if applied { 1 } else { 3 };
        return;
    }
    let request_id = ids.allocate();
    let action = match request.kind {
        1 if bridge
            .features
            .catalog
            .iter()
            .any(|entry| entry.weapon == request.weapon) =>
        {
            sim::ClientAction::GiveWeapon {
                request_id,
                weapon: request.weapon,
                model: 0,
            }
        }
        2 => sim::ClientAction::ResupplyAmmo { request_id },
        _ => {
            bridge.features.ack = request.sequence;
            bridge.features.outcome = 3;
            return;
        }
    };
    match inbox.push(local.0, action) {
        Ok(()) => bridge.features.pending = Some((request.sequence, request_id)),
        Err(_) => {
            bridge.features.ack = request.sequence;
            bridge.features.outcome = 3;
        }
    }
}
fn position(bridge: &Bridge, origin: [f32; 3]) -> [f32; 3] {
    let anchor = bridge.anchor.unwrap_or(bridge.host_movement.foot);
    std::array::from_fn(|i| anchor[i] + (origin[i] - bridge.engine_anchor[i]) / UNITS_PER_METER)
}
pub(super) fn export(
    bridge: &mut Bridge,
    result: &sim::TickResult,
    world: &sim::SimWorld,
    local: sim::ClientId,
) {
    if bridge.features.catalog.is_empty() && bridge.guest.ready != 0 {
        let names = world.weapon_script_names();
        let mut candidates: Vec<_> = names
            .iter()
            .enumerate()
            .filter(|(id, name)| {
                *id != 0
                    && name.ends_with("_mp")
                    && !NOT_HANDHELD.iter().any(|prefix| name.starts_with(prefix))
                    && world.weapon_runnable(*id as u32)
            })
            .collect();
        // Base weapons and all equipment (claymore, C4, grenades, knife) come
        // before attachment permutations, which fill the remaining slots.
        candidates.sort_by_key(|(id, name)| {
            let equipment = world
                .equipment_facts_for(*id as u32)
                .is_some_and(|f| f.is_offhand());
            (
                if equipment {
                    0
                } else {
                    name.matches('_').count()
                },
                name.as_str(),
            )
        });
        for (id, script_name) in candidates.into_iter().take(128) {
            let kind = world
                .equipment_facts_for(id as u32)
                .map_or(0, |f| f.offhand_class as u32);
            let mut entry = Entry {
                weapon: id as u32,
                kind,
                name: [0; 56],
            };
            let label = script_name
                .rsplit(['/', ':'])
                .next()
                .unwrap_or(script_name)
                .trim_end_matches("_mp")
                .replace('_', " ");
            let n = label.len().min(55);
            entry.name[..n].copy_from_slice(&label.as_bytes()[..n]);
            bridge.features.catalog.push(entry);
        }
        bridge.features.catalog.sort_by(|a, b| a.name.cmp(&b.name));
        diag::info!(
            Launch,
            "GTA catalog: {} weapons, cheats={}",
            bridge.features.catalog.len(),
            world.cheats_enabled()
        );
    }
    if let Some((sequence, request_id)) = bridge.features.pending {
        for action in &result.action_results {
            let id = match action.action {
                sim::ClientAction::GiveWeapon { request_id, .. }
                | sim::ClientAction::ResupplyAmmo { request_id } => request_id,
                _ => continue,
            };
            if action.client == local && id == request_id {
                bridge.features.ack = sequence;
                bridge.features.outcome = if action.outcome == sim::ActionOutcome::Applied {
                    1
                } else {
                    3
                };
                bridge.features.pending = None;
                if action.outcome == sim::ActionOutcome::Applied {
                    if let sim::ClientAction::GiveWeapon { weapon, .. } = action.action {
                        if world
                            .equipment_facts_for(weapon)
                            .is_none_or(|f| !f.is_offhand())
                        {
                            bridge.features.forced_selection = Some(weapon);
                        }
                    }
                }
                diag::info!(
                    Launch,
                    "GTA inventory request: {} {:?} action={:?} player={:?}",
                    sequence,
                    action.outcome,
                    action.action,
                    world
                        .player(local)
                        .map(|ps| (ps.weapon, ps.health, ps.origin))
                );
            }
        }
    }
    let mut projectiles = Vec::with_capacity(32);
    if host_has_player(bridge) {
        for p in result
            .snapshot
            .projectiles
            .iter()
            .filter(|p| p.owner == local)
            .take(32)
        {
            let kind = world
                .equipment_facts_for(p.weapon)
                .map_or(0, |f| f.projectile_explosion_type as u32);
            projectiles.push(Projectile {
                id: p.id.0,
                weapon: p.weapon,
                origin: position(bridge, p.origin),
                angles: p.apos.tr_base,
                kind,
                tick: bridge.host.tick,
                ..Default::default()
            });
        }
    }
    let mut explosions = Vec::new();
    for event in &result.snapshot.meta.entity_events {
        if event.sequence.0 <= bridge.features.event {
            continue;
        }
        bridge.features.event = event.sequence.0;
        if event.payload.attacker_entity_num != local.0 as i32 || !host_has_player(bridge) {
            continue;
        }
        if event.event != entity_iw4::EntityEventKind::GRENADE_EXPLODE
            && event.event != entity_iw4::EntityEventKind::FLASHBANG_EXPLODE
        {
            continue;
        }
        let Some(facts) = world.equipment_facts_for(event.payload.weapon) else {
            continue;
        };
        bridge.features.explosions = bridge.features.explosions.wrapping_add(1);
        explosions.push(Explosion {
            sequence: bridge.features.explosions,
            id: event.payload.correlation,
            weapon: event.payload.weapon,
            kind: facts.projectile_explosion_type as u32,
            origin: position(bridge, event.payload.origin),
            radius: facts.explosion_radius.max(facts.explosion_radius_min) as f32 / UNITS_PER_METER,
            inner_damage: facts.explosion_inner_damage as f32,
            outer_damage: facts.explosion_outer_damage as f32,
            tick: bridge.host.tick,
            ..Default::default()
        });
        diag::info!(
            Launch,
            "GTA accepted explosion: id={} weapon={} position={:?}",
            event.payload.correlation,
            event.payload.weapon,
            event.payload.origin
        );
    }
    // Script-spawned classes may have no PersonalClass metadata. Read the
    // actual offhand inventory and IW4 ammo tables, as the weapon code does.
    let ps = world.player(local);
    let ammo = |primary: bool| {
        let Some(ps) = ps else { return 0 };
        let class = if primary {
            ps.offhand_primary
        } else {
            ps.offhand_secondary
        };
        ps.weapons
            .iter()
            .filter(|&&weapon| weapon > 0)
            .find_map(|&weapon| {
                let weapon = weapon as u32;
                let equipment = world.equipment_facts_for(weapon)?;
                if equipment.offhand_class != class || class == 0 {
                    return None;
                }
                let facts = world.weapon_combat_facts(weapon)?;
                Some(weapon_iw4::get_ammo_player_both_clips(
                    ps,
                    weapon,
                    weapon_iw4::ammo_table_key(facts.ammo_index, weapon),
                    weapon_iw4::clip_table_key(facts.clip_index, weapon),
                ))
            })
            .unwrap_or(0)
    };
    let lethal = ammo(true);
    let tactical = ammo(false);
    let memory = bridge.memory.clone();
    if let Some(_guard) = memory.lock(false) {
        unsafe {
            memory.write(BASE + 12, bridge.features.ack);
            memory.write(BASE + 16, bridge.features.outcome);
            memory.write(BASE + 20, bridge.features.catalog.len() as u32);
            memory.write(BASE + 24, projectiles.len() as u32);
            memory.write(BASE + 28, bridge.features.explosions);
            memory.write(BASE + 32, bridge.host.tick);
            memory.write(BASE + 36, lethal);
            memory.write(BASE + 40, tactical);
            memory.write(BASE + 44, ps.map_or(0, |ps| ps.health));
            for (i, entry) in bridge.features.catalog.iter().enumerate() {
                memory.write(CATALOG + i * 64, *entry)
            }
            for (i, p) in projectiles.iter().enumerate() {
                memory.write(PROJECTILES + i * 48, *p)
            }
            for e in explosions {
                memory.write(EXPLOSIONS + (e.sequence as usize % 16) * 48, e)
            }
        }
    }
}
