//! Local 32/64-bit bridge. Win32 memory access stays behind named mutexes.
use super::gta_collision::{HostCollision, HostTriangle, UNITS_PER_METER};
use bevy::camera::{CameraUpdateSystems, RenderTarget};
use bevy::prelude::*;
use std::ffi::c_void;
use std::sync::Arc;
use std::sync::atomic::Ordering;
use std::time::{Duration, Instant};
mod features;
mod profiling;
mod readback;

const MAGIC: u32 = 0x4154474d;
const SCENE: usize = 2097152;
const TRIANGLES: usize = SCENE + 64;
const MAX_TRIANGLES: usize = 8192;
const MAX_WIDTH: u32 = 1280;
const MAX_HEIGHT: u32 = 720;
const PIXELS: usize = features::END;
const SIZE: usize = PIXELS + MAX_WIDTH as usize * MAX_HEIGHT as usize * 4;
const HOST_MOVEMENT: usize = 116;
const GUEST_MOVEMENT: usize = 164;
const HOST_LOCK: usize = 228;
const CAMERA: usize = 4352;
const HOST: usize = 8;
const GUEST: usize = 68;
const SHOTS: usize = 256;
const FRAME: usize = 8192;
// Protocol v4 leaves the old image region free for memory-only diagnostics.
const TIMINGS: usize = 12288;
const TIMING_COUNT: usize = 2048;
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Timing {
    sequence: u32,
    tick: u32,
    frame_ms: f32,
    update_ms: f32,
    host_ms: f32,
    scene_ms: f32,
    capture_ms: f32,
    pending: u32,
    ready: u32,
    active: u32,
}
const _: () = assert!(size_of::<Timing>() == 40);
const _: () = assert!(TIMINGS + 64 + TIMING_COUNT * 40 < 1929216);
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Host {
    tick: u32,
    enabled: u32,
    yaw: f32,
    pitch: f32,
    fov: f32,
    dt: f32,
    velocity: [f32; 3],
    eye: [f32; 3],
    buttons: u32,
    health: f32,
    shutdown: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Guest {
    tick: u32,
    ready: u32,
    clip: i32,
    stock: i32,
    shots: u32,
    reserved: [u32; 3],
    ads: f32,
    kick_pitch: f32,
    kick_yaw: f32,
    dt: f32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct HostMovement {
    epoch: u32,
    tick: u32,
    scene_sequence: u32,
    flags: u32,
    foot: [f32; 3],
    reserved: [u32; 5],
}
/// What the host's aim selects for lock-on launchers, in host coordinates.
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct HostLock {
    /// 1 aim point valid, 2 aim is a vehicle, 8 track point valid.
    flags: u32,
    aim: [f32; 3],
    /// The last vehicle locked, followed while its missiles are in flight.
    track: [f32; 3],
}
const _: () = assert!(size_of::<HostLock>() == 28 && HOST_LOCK + 28 == SHOTS);
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct GuestMovement {
    tick: u32,
    epoch: u32,
    foot: [f32; 3],
    velocity: [f32; 3],
    view_height: f32,
    pm_flags: u32,
    bob_cycle: u32,
    grounded: u32,
    dt: f32,
    sequence: u32,
    reserved: [u32; 2],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct GuestCamera {
    tick: u32,
    epoch: u32,
    eye: [f32; 3],
    forward: [f32; 3],
    right: [f32; 3],
    up: [f32; 3],
    fov: f32,
    sequence: u32,
}
const _: () = assert!(size_of::<GuestCamera>() == 64);
const _: () = assert!(size_of::<HostMovement>() == 48 && size_of::<GuestMovement>() == 64);
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Shot {
    sequence: u32,
    weapon: u32,
    origin: [f32; 3],
    direction: [f32; 3],
    range: f32,
    damage: f32,
    min_damage: f32,
    max_damage_range: f32,
    min_damage_range: f32,
    host_tick: u32,
    reserved: [u32; 2],
}
const _: () =
    assert!(size_of::<Host>() == 60 && size_of::<Guest>() == 48 && size_of::<Shot>() == 64);
#[link(name = "kernel32")]
unsafe extern "system" {
    fn OpenFileMappingW(access: u32, inherit: i32, name: *const u16) -> *mut c_void;
    fn MapViewOfFile(
        handle: *mut c_void,
        access: u32,
        high: u32,
        low: u32,
        size: usize,
    ) -> *mut c_void;
    fn CreateMutexW(attributes: *const c_void, owner: i32, name: *const u16) -> *mut c_void;
    fn WaitForSingleObject(handle: *mut c_void, timeout: u32) -> u32;
    fn ReleaseMutex(handle: *mut c_void) -> i32;
    fn UnmapViewOfFile(address: *const c_void) -> i32;
    fn CloseHandle(handle: *mut c_void) -> i32;
    fn GetTickCount() -> u32;
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
}
fn host_process_alive(memory: &Memory) -> bool {
    let Some(_guard) = memory.lock(false) else {
        return true;
    };
    let pid = unsafe { memory.read::<u32>(features::BASE + 60) };
    if pid == 0 {
        return false;
    }
    unsafe {
        let process = OpenProcess(0x100000, 0, pid);
        if process.is_null() {
            return false;
        }
        let running = WaitForSingleObject(process, 0) == 0x102;
        CloseHandle(process);
        running
    }
}
struct Memory {
    mapping: usize,
    address: usize,
    control: usize,
    frame: usize,
    scene: usize,
}
impl Drop for Memory {
    fn drop(&mut self) {
        unsafe {
            UnmapViewOfFile(self.address as *const c_void);
            for handle in [self.mapping, self.control, self.frame, self.scene] {
                CloseHandle(handle as *mut c_void);
            }
        }
    }
}
struct Guard(usize);
impl Drop for Guard {
    fn drop(&mut self) {
        unsafe {
            ReleaseMutex(self.0 as *mut c_void);
        }
    }
}
impl Memory {
    fn lock(&self, image: bool) -> Option<Guard> {
        let handle = if image { self.frame } else { self.control };
        let status = unsafe { WaitForSingleObject(handle as *mut c_void, 0) };
        (status == 0 || status == 0x80).then_some(Guard(handle))
    }
    fn lock_scene(&self) -> Option<Guard> {
        let status = unsafe { WaitForSingleObject(self.scene as *mut c_void, 0) };
        (status == 0 || status == 0x80).then_some(Guard(self.scene))
    }
    fn open() -> Option<Arc<Self>> {
        let name = std::env::var("IW4L_GTA_BRIDGE").ok()?;
        let wide = |suffix: &str| {
            format!("Local\\{name}{suffix}\0")
                .encode_utf16()
                .collect::<Vec<_>>()
        };
        unsafe {
            let mapping = OpenFileMappingW(0xf001f, 0, wide("").as_ptr());
            if mapping.is_null() {
                return None;
            }
            let address = MapViewOfFile(mapping, 0xf001f, 0, 0, SIZE);
            if address.is_null() {
                CloseHandle(mapping);
                return None;
            }
            let control = CreateMutexW(std::ptr::null(), 0, wide("_control").as_ptr());
            let frame = CreateMutexW(std::ptr::null(), 0, wide("_frame").as_ptr());
            let scene = CreateMutexW(std::ptr::null(), 0, wide("_scene").as_ptr());
            if control.is_null() || frame.is_null() || scene.is_null() {
                UnmapViewOfFile(address);
                CloseHandle(mapping);
                if !control.is_null() {
                    CloseHandle(control);
                }
                if !frame.is_null() {
                    CloseHandle(frame);
                }
                if !scene.is_null() {
                    CloseHandle(scene);
                }
                return None;
            }
            Some(Arc::new(Self {
                mapping: mapping as usize,
                address: address as usize,
                control: control as usize,
                frame: frame as usize,
                scene: scene as usize,
            }))
        }
    }
    // Callers hold the corresponding mutex, and all records have fixed checked bounds.
    unsafe fn read<T: Copy>(&self, offset: usize) -> T {
        assert!(offset + size_of::<T>() <= SIZE);
        unsafe { std::ptr::read_unaligned((self.address + offset) as *const T) }
    }
    unsafe fn write<T: Copy>(&self, offset: usize, value: T) {
        assert!(offset + size_of::<T>() <= SIZE);
        unsafe {
            std::ptr::write_unaligned((self.address + offset) as *mut T, value);
        }
    }
}
#[derive(Resource)]
struct Bridge {
    memory: Arc<Memory>,
    host: Host,
    guest: Guest,
    queue: Vec<Shot>,
    capture_status: Arc<readback::Status>,
    last_update: Instant,
    timing: Timing,
    sleep_timing: [f32; 2],
    phase_start: Instant,
    phases: [f32; 8],
    overlay: Option<Handle<Image>>,
    overlay_size: UVec2,
    last_tick: u32,
    last_cycle: bool,
    anchor: Option<[f32; 3]>,
    engine_anchor: [f32; 3],
    host_movement: HostMovement,
    host_lock: HostLock,
    lock_started: Option<(u32, Instant)>,
    movement: GuestMovement,
    movement_epoch: u32,
    scene_sequence: u32,
    installed_scene: u32,
    triangles: Vec<HostTriangle>,
    collision: Option<Arc<HostCollision>>,
    /// Host entities temporarily released because they trapped the player.
    ghosts: Vec<u32>,
    look_epoch: u32,
    mouse_total: [f32; 2],
    camera_sequence: u32,
    features: features::State,
}
pub fn install(app: &mut App) {
    if std::env::var_os("IW4L_GTA_BRIDGE").is_none() {
        return;
    }
    let Some(memory) = Memory::open() else {
        panic!("GTA bridge mapping is not open; launch using the GTA bridge launcher");
    };
    let capture_status = Arc::new(readback::Status::default());
    readback::install(app, memory.clone(), capture_status.clone());
    app.insert_resource(bevy::winit::WinitSettings::continuous());
    app.insert_resource(Bridge {
        memory,
        host: Host::default(),
        guest: Guest::default(),
        queue: Vec::new(),
        capture_status,
        last_update: Instant::now(),
        timing: Timing::default(),
        sleep_timing: [0.0; 2],
        phase_start: Instant::now(),
        phases: [0.0; 8],
        overlay: None,
        overlay_size: UVec2::ZERO,
        last_tick: 0,
        last_cycle: false,
        anchor: None,
        engine_anchor: [0.0; 3],
        host_movement: HostMovement::default(),
        host_lock: HostLock::default(),
        lock_started: None,
        movement: GuestMovement::default(),
        movement_epoch: 0,
        scene_sequence: 0,
        installed_scene: 0,
        triangles: Vec::new(),
        collision: None,
        ghosts: Vec::new(),
        look_epoch: 0,
        mouse_total: [0.0; 2],
        camera_sequence: 0,
        features: features::State::default(),
    });
    if std::env::var_os("IW4L_GTA_PHASE_DIAGNOSTICS").is_some() {
        profiling::install(app);
    }
    app.add_systems(
        First,
        (pace_runtime, read_host, install_client_scene).chain(),
    );
    app.add_systems(
        PostUpdate,
        prepare_overlay_target.before(CameraUpdateSystems),
    );
    app.add_systems(
        Update,
        host_keys
            .in_set(frame::ClientSet::Input)
            .before(net::sample_client_input),
    );
    app.add_systems(
        Update,
        features::host_actions
            .in_set(frame::ClientSet::Input)
            .before(net::sample_client_input),
    );
    app.add_systems(
        Update,
        host_input
            .in_set(frame::ClientSet::Input)
            .after(net::sample_client_input),
    );
    app.add_systems(
        FixedUpdate,
        (
            anchor_authority
                .before(frame::AuthoritySet::Step)
                .after(frame::AuthoritySet::Gather),
            host_lock
                .after(anchor_authority)
                .before(frame::AuthoritySet::Step),
            export_shots
                .after(frame::AuthoritySet::Step)
                .before(frame::AuthoritySet::Snapshot),
        ),
    );
    app.add_systems(
        Last,
        (
            export_movement,
            export_camera,
            publish_control,
            publish_timings,
        )
            .chain(),
    );
}
fn pace_runtime(mut bridge: ResMut<Bridge>) {
    bridge.sleep_timing = [0.0; 2];
    if bridge.guest.ready != 0 {
        let period = Duration::from_micros(if bridge.host_movement.flags & 1 != 0 {
            16_667
        } else {
            66_667
        });
        if let Some(wait) = period.checked_sub(bridge.last_update.elapsed()) {
            let started = Instant::now();
            bridge.sleep_timing[0] = wait.as_secs_f32() * 1000.0;
            std::thread::sleep(wait);
            bridge.sleep_timing[1] = started.elapsed().as_secs_f32() * 1000.0;
        }
    }
    let now = Instant::now();
    bridge.timing.frame_ms = now.duration_since(bridge.last_update).as_secs_f32() * 1000.0;
    bridge.timing.host_ms = 0.0;
    bridge.timing.scene_ms = 0.0;
    bridge.last_update = now;
}
fn prepare_overlay_target(
    mut bridge: ResMut<Bridge>,
    windows: Query<&Window, With<bevy::window::PrimaryWindow>>,
    mut images: ResMut<Assets<Image>>,
    mut targets: Query<&mut RenderTarget, With<Camera>>,
) {
    let Ok(window) = windows.single() else {
        return;
    };
    let size = UVec2::new(
        window.physical_width().clamp(1, MAX_WIDTH),
        window.physical_height().clamp(1, MAX_HEIGHT),
    );
    let previous = bridge.overlay.clone();
    if bridge.overlay_size != size {
        // All cameras (world, weapon and UI) share one offscreen target. A
        // hidden DXGI swapchain must not pace the host's input or animation.
        let mut image = Image::new_target_texture(
            size.x,
            size.y,
            bevy::render::render_resource::TextureFormat::Bgra8UnormSrgb,
            None,
        );
        image.texture_descriptor.usage |= bevy::render::render_resource::TextureUsages::COPY_SRC;
        bridge.overlay = Some(images.add(image));
        bridge.overlay_size = size;
    }
    let Some(image) = &bridge.overlay else {
        return;
    };
    for mut target in &mut targets {
        if target.as_image() != Some(image)
            && (matches!(*target, RenderTarget::Window(_))
                || previous
                    .as_ref()
                    .is_some_and(|old| target.as_image() == Some(old)))
        {
            *target = RenderTarget::Image(image.clone().into());
        }
    }
}
fn read_host(mut bridge: ResMut<Bridge>, mut exit: MessageWriter<AppExit>) {
    let started = Instant::now();
    let memory = bridge.memory.clone();
    if let Some(_guard) = memory.lock(false) {
        if unsafe { memory.read::<u32>(0) } != MAGIC || unsafe { memory.read::<u32>(4) } != 4 {
            exit.write(AppExit::error());
            return;
        }
        bridge.host = unsafe { memory.read(HOST) };
        bridge.host_movement = unsafe { memory.read(HOST_MOVEMENT) };
        bridge.host_lock = unsafe { memory.read(HOST_LOCK) };
        bridge.features.request = unsafe { memory.read(features::BASE) };
    }
    if let Some(_guard) = memory.lock_scene() {
        let sequence = unsafe { memory.read::<u32>(SCENE) };
        let count = unsafe { memory.read::<u32>(SCENE + 4) } as usize;
        if sequence != bridge.scene_sequence && count > 0 && count <= MAX_TRIANGLES {
            bridge.triangles = unsafe {
                std::slice::from_raw_parts(
                    (memory.address + TRIANGLES) as *const HostTriangle,
                    count,
                )
            }
            .to_vec();
            bridge.scene_sequence = sequence;
        }
    }
    let now = unsafe { GetTickCount() };
    if bridge.host.shutdown != 0
        || (bridge.host.tick != 0
            && now.wrapping_sub(bridge.host.tick) > 15000
            && !host_process_alive(&memory))
    {
        exit.write(AppExit::Success);
    }
    bridge.timing.host_ms = started.elapsed().as_secs_f32() * 1000.0;
}
fn live(host: &Host) -> bool {
    host.enabled != 0 && unsafe { GetTickCount() }.wrapping_sub(host.tick) < 1000
}
fn host_has_player(bridge: &Bridge) -> bool {
    bridge.host_movement.flags & 1 != 0
        && unsafe { GetTickCount() }.wrapping_sub(bridge.host.tick) < 1000
}
fn install_client_scene(
    bridge: Res<Bridge>,
    prediction: Option<ResMut<net::ClientPredictionState>>,
) {
    if let (Some(scene), Some(mut prediction)) = (&bridge.collision, prediction) {
        prediction.0.set_external_movement_backend(scene.clone());
    }
}
fn host_keys(
    mut bridge: ResMut<Bridge>,
    mut actions: ResMut<net::ClientActionInput>,
    mut look: ResMut<net::LookState>,
    presented: Res<net::PresentedSnapshot>,
    local: Res<net::LocalPresentClient>,
    mut settings: ResMut<frame::GameSettings>,
) {
    let buttons = if live(&bridge.host) {
        bridge.host.buttons
    } else {
        0
    };
    let now = actions.now_msec.max(1);
    let msec = actions.frame_msec.max(1);
    for (name, bit) in [
        ("+attack", 1),
        // MW2's aim bind: also the throw button, which fires the left akimbo gun.
        ("+speed_throw", 2),
        ("+reload", 4),
        ("+forward", 16),
        ("+back", 32),
        ("+moveleft", 64),
        ("+moveright", 128),
        ("+breath_sprint", 256),
        ("+gostand", 512),
        ("+movedown", 1024),
        ("+prone", 2048),
        ("+frag", 4096),
        ("+smoke", 8192),
        ("+melee", 16384),
        ("+activate", 32768),
    ] {
        let Some(down) = input_iw4::command_id_from_name(name) else {
            continue;
        };
        let id = if buttons & bit != 0 {
            down
        } else {
            input_iw4::key_up_command_id(down).unwrap_or(down)
        };
        input_iw4::input_cmd(&mut actions.client, id, input_iw4::SCRIPT_KEYNUM, now, msec);
    }
    let cycle = buttons & 8 != 0;
    if cycle && !bridge.last_cycle {
        if let Some(id) = input_iw4::command_id_from_name("weapnext") {
            input_iw4::input_cmd(&mut actions.client, id, input_iw4::SCRIPT_KEYNUM, now, msec);
        }
    }
    bridge.last_cycle = cycle;
    actions.client.kb.speed.active = false;
    input_iw4::set_ads(&mut actions.client, buttons & 2 != 0);
    settings.fov = bridge.host.fov;
    let total = [
        f32::from_bits(bridge.host_movement.reserved[0]),
        f32::from_bits(bridge.host_movement.reserved[1]),
    ];
    if presented.player(local.0).is_some()
        && (bridge.look_epoch != bridge.host_movement.epoch
            || bridge.host_movement.reserved[2] != 0)
    {
        let delta = presented
            .player(local.0)
            .map_or([0.0; 3], |ps| ps.delta_angles);
        look.angles = net::look_angles_from_degrees([
            bridge.host.pitch - delta[0],
            bridge.host.yaw - delta[1],
            -delta[2],
        ]);
        bridge.look_epoch = bridge.host_movement.epoch;
        bridge.mouse_total = total;
    }
    // Cumulative samples preserve every host delta even when the two engines
    // run at different rates. IW4's usercmd code owns zoom sensitivity and look.
    actions.mouse_x = if live(&bridge.host) {
        total[0] - bridge.mouse_total[0]
    } else {
        0.0
    };
    actions.mouse_y = if live(&bridge.host) {
        total[1] - bridge.mouse_total[1]
    } else {
        0.0
    };
    bridge.mouse_total = total;
    actions.pad_look_delta = [0.0; 2];
}
fn host_input(bridge: Res<Bridge>, mut input: ResMut<net::ClientCmdTemplate>) {
    if !live(&bridge.host) {
        input.cmd.forwardmove = 0;
        input.cmd.rightmove = 0;
        input.cmd.buttons = 0;
    }
}
fn anchor_authority(
    mut bridge: ResMut<Bridge>,
    world: Option<ResMut<net::AuthorityWorld>>,
    local: Option<Res<net::LocalPresentClient>>,
    prediction: Option<ResMut<net::ClientPredictionState>>,
) {
    let (Some(mut world), Some(local)) = (world, local) else {
        return;
    };
    if bridge.guest.ready == 0
        || !live(&bridge.host)
        || bridge.host_movement.epoch == 0
        || bridge.triangles.is_empty()
    {
        return;
    }
    if world.0.player(local.0).is_none_or(|ps| ps.pm_type != 0) {
        return;
    }
    let changed_epoch = bridge.movement_epoch != bridge.host_movement.epoch;
    if changed_epoch {
        bridge.anchor = Some(bridge.host_movement.foot);
        bridge.engine_anchor = world.0.player(local.0).unwrap().origin;
        // The capsule trace needs its normal contact clearance. Keep the
        // existing IW4 player pose and put the converted host floor just below
        // it, rather than starting the capsule inside a thin surface prism.
        bridge.engine_anchor[2] -= 0.4;
        diag::info!(
            Launch,
            "GTA movement activation: epoch={} gta={:?} iw4={:?}",
            bridge.host_movement.epoch,
            bridge.anchor,
            bridge.engine_anchor
        );
        bridge.movement_epoch = bridge.host_movement.epoch;
        // Preserve IW4's spawn coordinates and prediction history. Convert
        // between engine anchors instead of relocating its player to (0,0,0).
    }
    if changed_epoch || bridge.installed_scene != bridge.scene_sequence {
        let started = Instant::now();
        let mut scene = HostCollision::from_triangles_at(
            &bridge.triangles,
            bridge.anchor.unwrap(),
            bridge.engine_anchor,
        );
        if scene.is_empty() {
            return;
        }
        if changed_epoch {
            bridge.ghosts.clear();
        }
        let ps = world.0.player(local.0).unwrap();
        let maxs_z = if ps.pm_flags & playerstate_iw4::pm_flags::PRONE != 0 {
            movement_iw4::PRONE_MAXS_Z
        } else if ps.pm_flags & playerstate_iw4::pm_flags::CROUCH != 0 {
            movement_iw4::CROUCH_MAXS_Z
        } else {
            movement_iw4::STAND_MAXS_Z
        };
        let ghosted = bridge.ghosts.len();
        let released = scene.release_traps(
            ps.origin,
            [-15.0, -15.0, 0.0],
            [15.0, 15.0, maxs_z],
            0x0281_0011,
            &mut bridge.ghosts,
        );
        if bridge.ghosts.len() > ghosted {
            diag::info!(
                Launch,
                "GTA collision trap released: surfaces={} entities={:?} origin={:?}",
                released,
                bridge.ghosts,
                ps.origin
            );
        }
        let scene = Arc::new(scene);
        world
            .0
            .set_external_movement_backend(local.0, scene.clone());
        if let Some(mut prediction) = prediction {
            prediction.0.set_external_movement_backend(scene.clone());
        }
        bridge.collision = Some(scene);
        bridge.installed_scene = bridge.scene_sequence;
        bridge.timing.scene_ms += started.elapsed().as_secs_f32() * 1000.0;
    }
}
/// GTA vehicles and ground are not IW4 entities, so the launcher scripts'
/// own lock traces cannot find them. Acquire on the host's aim with the same
/// lock states, and keep guided missiles following the locked vehicle.
fn host_lock(
    mut bridge: ResMut<Bridge>,
    world: Option<ResMut<net::AuthorityWorld>>,
    local: Option<Res<net::LocalPresentClient>>,
    prediction: Option<ResMut<net::ClientPredictionState>>,
) {
    const ACQUIRE: Duration = Duration::from_millis(1500);
    let (Some(mut world), Some(local), Some(anchor)) = (world, local, bridge.anchor) else {
        return;
    };
    let Some(weapon) = world.0.player(local.0).map(|ps| ps.weapon) else {
        return;
    };
    let engine_anchor = bridge.engine_anchor;
    let engine = |p: [f32; 3]| -> [f32; 3] {
        std::array::from_fn(|i| engine_anchor[i] + (p[i] - anchor[i]) * UNITS_PER_METER)
    };
    let lock = bridge.host_lock;
    let launcher = world.0.lock_on_launcher(weapon);
    // Javelin locks onto ground or vehicles; other launchers need a vehicle.
    let aim = launcher
        .filter(|&top| {
            host_has_player(&bridge)
                && bridge.host.buttons & 2 != 0
                && lock.flags & 1 != 0
                && (top || lock.flags & 2 != 0)
        })
        .map(|_| engine(lock.aim));
    let top = launcher.unwrap_or(false);
    let locked = match (aim, bridge.lock_started) {
        (None, _) => {
            bridge.lock_started = None;
            false
        }
        (Some(_), Some((w, started))) if w == weapon => started.elapsed() >= ACQUIRE,
        (Some(_), _) => {
            bridge.lock_started = Some((weapon, Instant::now()));
            false
        }
    };
    let track = (lock.flags & 8 != 0).then(|| engine(lock.track));
    let state = aim.map(|point| (point, locked));
    world.0.set_host_weapon_lock(local.0, state, top, track);
    if let Some(mut prediction) = prediction {
        prediction.0.set_host_weapon_lock(state, top, track);
    }
}
fn export_movement(
    mut bridge: ResMut<Bridge>,
    presented: Res<net::PresentedSnapshot>,
    local: Res<net::LocalPresentClient>,
    time: Res<Time<Real>>,
) {
    if !host_has_player(&bridge) {
        return;
    }
    let (Some(anchor), Some(ps)) = (bridge.anchor, presented.player(local.0)) else {
        return;
    };
    // Export the genuine IW4 client prediction, at the same cadence and from
    // the same playerstate used by its camera, weapon animation and HUD.
    bridge.movement.sequence = bridge.movement.sequence.wrapping_add(1);
    bridge.movement.tick = unsafe { GetTickCount() };
    bridge.movement.epoch = bridge.movement_epoch;
    bridge.movement.foot = std::array::from_fn(|i| {
        anchor[i] + (ps.origin[i] - bridge.engine_anchor[i]) / UNITS_PER_METER
    });
    bridge.movement.velocity = ps.velocity.map(|v| v / UNITS_PER_METER);
    bridge.movement.view_height = ps.view_height_current / UNITS_PER_METER;
    bridge.movement.pm_flags = ps.pm_flags;
    bridge.movement.bob_cycle = ps.bob_cycle as u32;
    bridge.movement.grounded = u32::from(ps.ground_entity_num != playerstate_iw4::ENTITYNUM_NONE);
    bridge.movement.dt = time.delta_secs();
    let memory = bridge.memory.clone();
    if let Some(_guard) = memory.lock(false) {
        unsafe {
            memory.write(GUEST_MOVEMENT, bridge.movement);
        }
    }
}
fn export_camera(mut bridge: ResMut<Bridge>, view: Res<render_scene::PreparedSceneView>) {
    let Some(anchor) = bridge.anchor else {
        return;
    };
    if !view.ready || !host_has_player(&bridge) {
        return;
    }
    let world_from_view = view.view_from_world.inverse();
    let fov = 2.0 * ((view.fov * 0.5).tan() * view.aspect).atan().to_degrees();
    if !world_from_view.is_finite() || !fov.is_finite() {
        return;
    }
    bridge.camera_sequence = bridge.camera_sequence.wrapping_add(1);
    let camera = GuestCamera {
        tick: unsafe { GetTickCount() },
        epoch: bridge.movement_epoch,
        eye: std::array::from_fn(|i| {
            anchor[i] + (view.eye[i] - bridge.engine_anchor[i]) / UNITS_PER_METER
        }),
        forward: (-world_from_view.z_axis.truncate()).to_array(),
        // GTA's camera right column has the opposite handedness to IW4/Bevy.
        right: (-world_from_view.x_axis.truncate()).to_array(),
        up: world_from_view.y_axis.truncate().to_array(),
        fov,
        sequence: bridge.camera_sequence,
    };
    let memory = bridge.memory.clone();
    if let Some(_guard) = memory.lock(false) {
        unsafe {
            memory.write(CAMERA, camera);
        }
    }
}
fn export_shots(
    mut bridge: ResMut<Bridge>,
    result: Option<Res<net::PendingStepResult>>,
    world: Option<Res<net::AuthorityWorld>>,
    local: Option<Res<net::LocalPresentClient>>,
) {
    let (Some(result), Some(world), Some(local)) = (result, world, local) else {
        return;
    };
    let Some(result) = result.0.as_ref() else {
        return;
    };
    if bridge.last_tick == result.snapshot.tick.0 {
        return;
    }
    bridge.last_tick = result.snapshot.tick.0;
    features::export(&mut bridge, result, &world.0, local.0);
    let host = bridge.host;
    if !live(&host) {
        return;
    }
    for emission in &result.external_emissions {
        if emission.attacker != local.0 || bridge.queue.len() >= 256 {
            continue;
        }
        let facts = world.0.weapon_combat_facts(emission.weapon);
        let origin = bridge.anchor.map_or(host.eye, |a| {
            std::array::from_fn(|i| {
                a[i] + (emission.origin[i] - bridge.engine_anchor[i]) / UNITS_PER_METER
            })
        });
        bridge.queue.push(Shot {
            weapon: emission.weapon,
            origin,
            direction: emission.direction,
            range: emission.max_range / 39.37008,
            damage: emission.base_damage as f32,
            min_damage: facts.map_or(emission.base_damage as f32, |f| f.min_damage as f32),
            max_damage_range: facts.map_or(0.0, |f| f.max_damage_range / 39.37008),
            min_damage_range: facts.map_or(0.0, |f| f.min_damage_range / 39.37008),
            host_tick: host.tick,
            ..Default::default()
        });
    }
}
fn publish_control(
    mut bridge: ResMut<Bridge>,
    presented: Res<net::PresentedSnapshot>,
    local: Res<net::LocalPresentClient>,
    screen: Res<frame::AppScreen>,
    time: Res<Time<Real>>,
    cmd: Res<net::ClientCmdTemplate>,
    world: Option<Res<net::AuthorityWorld>>,
) {
    bridge.guest.tick = unsafe { GetTickCount() };
    bridge.guest.ready = u32::from(
        *screen == frame::AppScreen::InGame
            && presented.player(local.0).is_some_and(|p| p.pm_type == 0),
    );
    bridge.guest.dt = time.delta_secs();
    if let Some(ps) = presented.player(local.0) {
        bridge.guest.ads = ps.f_weapon_pos_frac;
        bridge.guest.reserved[0] = ps.weapon;
        bridge.guest.reserved[1] = bridge.engine_anchor[0].to_bits();
        bridge.guest.reserved[2] = bridge.engine_anchor[2].to_bits();
        if bridge.movement.sequence != 0 && bridge.movement.sequence % 60 == 0 {
            let authority = world.as_ref().and_then(|w| w.0.player(local.0));
            diag::info!(
                Launch,
                "GTA input: host={} cmd={} axes={}/{} pred-weapon={} state={} time={} flags={} auth={:?}",
                bridge.host.buttons,
                cmd.cmd.buttons,
                cmd.cmd.forwardmove,
                cmd.cmd.rightmove,
                ps.weapon,
                ps.weaponstate_primary,
                ps.weapon_time,
                ps.weap_flags,
                authority.map(|a| (
                    a.weapon,
                    a.weaponstate_primary,
                    a.weapon_time,
                    a.weap_flags,
                    a.origin,
                    a.health
                ))
            );
        }
    }
    if let Some(meta) = presented
        .snapshot()
        .and_then(|s| s.meta.for_client(local.0))
    {
        bridge.guest.clip = meta.ammo_clip;
        bridge.guest.stock = meta.ammo_stock;
    }
    let memory = bridge.memory.clone();
    if let Some(_guard) = memory.lock(false) {
        let queued = std::mem::take(&mut bridge.queue);
        for mut shot in queued {
            bridge.guest.shots = bridge.guest.shots.wrapping_add(1);
            shot.sequence = bridge.guest.shots;
            unsafe {
                memory.write(SHOTS + (shot.sequence as usize % 64) * 64, shot);
            }
        }
        unsafe {
            memory.write(GUEST, bridge.guest);
        }
    }
}
fn publish_timings(mut bridge: ResMut<Bridge>) {
    let memory = bridge.memory.clone();
    let Some(_guard) = memory.lock(false) else {
        return;
    };
    bridge.timing.sequence = bridge.timing.sequence.wrapping_add(1);
    bridge.timing.tick = unsafe { GetTickCount() };
    bridge.timing.update_ms = bridge.last_update.elapsed().as_secs_f32() * 1000.0;
    bridge.timing.pending = bridge.capture_status.pending.load(Ordering::Acquire);
    bridge.timing.capture_ms = bridge.capture_status.latency.load(Ordering::Acquire) as f32;
    bridge.timing.ready = bridge.guest.ready;
    bridge.timing.active = u32::from(host_has_player(&bridge));
    let sequence = bridge.timing.sequence;
    unsafe {
        memory.write(
            TIMINGS + 64 + TIMING_COUNT * 40 + (sequence as usize % TIMING_COUNT) * 8,
            bridge.sleep_timing,
        );
        memory.write(
            TIMINGS + 64 + (sequence as usize % TIMING_COUNT) * 40,
            bridge.timing,
        );
        memory.write(TIMINGS, sequence);
    }
}
