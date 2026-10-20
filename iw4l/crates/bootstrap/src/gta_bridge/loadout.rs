//! Remembers the player's guns and equipment across runtime starts, so the F8
//! loadout comes back after GTA restarts, a save is loaded or MW2 restarts.
use super::*;
use playerstate_iw4::PlayerState;
use std::path::PathBuf;

const FILE: &str = "gta-loadout.txt";

#[derive(Clone, Default, PartialEq, Eq, Debug)]
pub(super) struct Saved {
    guns: Vec<String>,
    held: String,
    lethal: String,
    tactical: String,
}
impl Saved {
    /// The held gun changes with every weapon switch; only a different
    /// inventory is worth writing to disk.
    fn same_inventory(&self, other: &Saved) -> bool {
        self.guns == other.guns && self.lethal == other.lethal && self.tactical == other.tactical
    }
}
struct Target {
    guns: Vec<u32>,
    offhands: Vec<u32>,
    held: u32,
    started: Instant,
    held_selected: bool,
}
pub(super) enum Step {
    Give(u32),
    Select(u32),
}
#[derive(Default)]
pub(super) struct Loadout {
    loaded: bool,
    last: Option<Saved>,
    target: Option<Target>,
    /// Request id and weapon of the restore give in flight.
    pub pending: Option<(u32, u32, Instant)>,
    selecting: Option<(u32, Instant)>,
}
fn path() -> PathBuf {
    std::env::var_os("IW4L_SETTINGS_PATH")
        .map(PathBuf::from)
        .and_then(|settings| settings.parent().map(|dir| dir.join(FILE)))
        .unwrap_or_else(|| PathBuf::from(FILE))
}
fn read() -> Option<Saved> {
    let text = std::fs::read_to_string(path()).ok()?;
    let mut saved = Saved::default();
    for line in text.lines() {
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let value = value.trim().to_owned();
        match key.trim() {
            "gun" if saved.guns.len() < 2 => saved.guns.push(value),
            "held" => saved.held = value,
            "lethal" => saved.lethal = value,
            "tactical" => saved.tactical = value,
            _ => {}
        }
    }
    (!saved.guns.is_empty()).then_some(saved)
}
fn write(saved: &Saved) {
    let mut text = String::new();
    for gun in &saved.guns {
        text += &format!("gun={gun}\n");
    }
    text += &format!(
        "held={}\nlethal={}\ntactical={}\n",
        saved.held, saved.lethal, saved.tactical
    );
    let target = path();
    let temporary = target.with_extension("tmp");
    let result = std::fs::write(&temporary, text).and_then(|()| std::fs::rename(&temporary, &target));
    match result {
        Ok(()) => diag::info!(Launch, "GTA loadout saved: {saved:?}"),
        Err(error) => diag::info!(Launch, "GTA loadout not saved: {error}"),
    }
}
fn is_gun(world: &sim::SimWorld, weapon: u32) -> bool {
    weapon > 0
        && world
            .equipment_facts_for(weapon)
            .is_none_or(|facts| !facts.is_offhand())
        && !world
            .weapon_script_names()
            .get(weapon as usize)
            .is_none_or(|name| name.starts_with("killstreak_"))
}
fn owned(ps: &PlayerState) -> impl Iterator<Item = u32> + '_ {
    ps.weapons.iter().filter(|&&w| w > 0).map(|&w| w as u32)
}
fn snapshot(world: &sim::SimWorld, ps: &PlayerState) -> Saved {
    let names = world.weapon_script_names();
    let name = |weapon: u32| names.get(weapon as usize).cloned().unwrap_or_default();
    let mut saved = Saved::default();
    for weapon in owned(ps) {
        match world.equipment_facts_for(weapon).filter(|f| f.is_offhand()) {
            Some(f) if matches!(f.offhand_class, 1 | 4 | 5) => saved.lethal = name(weapon),
            Some(f) if matches!(f.offhand_class, 2 | 3) => saved.tactical = name(weapon),
            Some(_) => {}
            None if is_gun(world, weapon) && saved.guns.len() < 2 => saved.guns.push(name(weapon)),
            None => {}
        }
    }
    if is_gun(world, ps.weapon) {
        saved.held = name(ps.weapon);
    }
    saved
}
impl Loadout {
    pub(super) fn restoring(&self) -> bool {
        !self.loaded || self.target.is_some()
    }
    /// The next action that brings the inventory to the saved loadout. A give
    /// replaces the gun in hand, so a gun the saved loadout lacks is selected
    /// first and then replaced.
    pub(super) fn step(&mut self, world: &sim::SimWorld, ps: &PlayerState) -> Option<Step> {
        if !self.loaded {
            self.loaded = true;
            self.last = read();
            if let Some(saved) = &self.last {
                let id = |name: &str| {
                    world
                        .weapon_index_by_script_name(name)
                        .filter(|&w| world.weapon_runnable(w))
                };
                let guns: Vec<u32> = saved
                    .guns
                    .iter()
                    .filter_map(|n| id(n))
                    .filter(|&w| is_gun(world, w))
                    .collect();
                let offhands = [&saved.lethal, &saved.tactical]
                    .into_iter()
                    .filter_map(|n| id(n))
                    .filter(|&w| world.equipment_facts_for(w).is_some_and(|f| f.is_offhand()))
                    .collect();
                diag::info!(Launch, "GTA loadout restoring: {saved:?}");
                self.target = Some(Target {
                    guns,
                    offhands,
                    held: id(&saved.held).unwrap_or(0),
                    started: Instant::now(),
                    held_selected: false,
                });
            }
        }
        let target = self.target.as_mut()?;
        if target.started.elapsed() > Duration::from_secs(20) {
            diag::info!(Launch, "GTA loadout restore timed out");
            self.target = None;
            return None;
        }
        if self
            .pending
            .is_some_and(|(_, _, at)| at.elapsed() < Duration::from_secs(2))
        {
            return None;
        }
        self.pending = None;
        let has = |weapon: u32| owned(ps).any(|w| w == weapon);
        if let Some(&missing) = target.guns.iter().find(|&&g| !has(g)) {
            let spare = owned(ps).find(|&w| is_gun(world, w) && !target.guns.contains(&w));
            match spare {
                Some(spare) if ps.weapon == spare => {
                    self.selecting = None;
                    return Some(Step::Give(missing));
                }
                Some(spare) => {
                    if self
                        .selecting
                        .is_some_and(|(w, at)| w == spare && at.elapsed() < Duration::from_secs(3))
                    {
                        return None;
                    }
                    self.selecting = Some((spare, Instant::now()));
                    return Some(Step::Select(spare));
                }
                // Fewer guns than saved: a give would replace a saved gun.
                None => target.guns.retain(|&g| has(g)),
            }
        }
        if let Some(&missing) = target.offhands.iter().find(|&&o| !has(o)) {
            target.offhands.retain(|&o| o != missing);
            return Some(Step::Give(missing));
        }
        if target.held != 0 && ps.weapon != target.held && has(target.held) && !target.held_selected {
            target.held_selected = true;
            return Some(Step::Select(target.held));
        }
        diag::info!(Launch, "GTA loadout restored");
        self.target = None;
        None
    }
    /// Writes the inventory whenever it differs from the saved one.
    pub(super) fn save(&mut self, world: &sim::SimWorld, ps: &PlayerState) {
        if self.restoring() || ps.pm_type != 0 {
            return;
        }
        let current = snapshot(world, ps);
        if current.guns.is_empty() || self.last.as_ref().is_some_and(|last| last.same_inventory(&current)) {
            return;
        }
        write(&current);
        self.last = Some(current);
    }
}
