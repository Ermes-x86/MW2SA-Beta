use super::{Bridge, TIMING_COUNT};
use bevy::app::{MainScheduleOrder, PluginsState};
use bevy::ecs::schedule::ScheduleLabel;
use bevy::prelude::*;
use std::time::Instant;
pub(super) const PHASES: usize = 120000;
#[derive(ScheduleLabel, Debug, Clone, PartialEq, Eq, Hash)]
struct Begin;
#[derive(ScheduleLabel, Debug, Clone, PartialEq, Eq, Hash)]
struct Boundary(u8);
pub(super) fn install(app: &mut App) {
    {
        let mut order = app.world_mut().resource_mut::<MainScheduleOrder>();
        order.insert_before(First, Begin);
        order.insert_after(First, Boundary(0));
        order.insert_after(PreUpdate, Boundary(1));
        order.insert_after(bevy::app::RunFixedMainLoop, Boundary(2));
        order.insert_after(Update, Boundary(3));
        order.insert_after(PostUpdate, Boundary(4));
        order.insert_after(Last, Boundary(5));
    }
    app.add_systems(Begin, |mut bridge: ResMut<Bridge>| {
        bridge.phase_start = Instant::now();
        bridge.phases = [0.0; 8];
    });
    for index in 0..6 {
        app.add_systems(Boundary(index), move |mut bridge: ResMut<Bridge>| {
            bridge.phases[index as usize] = bridge.phase_start.elapsed().as_secs_f32() * 1000.0;
            bridge.phase_start = Instant::now();
        });
    }
    app.set_runner(run);
}
fn run(mut app: App) -> AppExit {
    if app.plugins_state() != PluginsState::Cleaned {
        while app.plugins_state() == PluginsState::Adding {
            bevy::tasks::tick_global_task_pools_on_main_thread();
        }
        app.finish();
        app.cleanup();
    }
    loop {
        let start = Instant::now();
        app.update();
        if let Some(bridge) = app.world().get_resource::<Bridge>() {
            let mut phases = bridge.phases;
            phases[6] = bridge.phase_start.elapsed().as_secs_f32() * 1000.0;
            phases[7] = start.elapsed().as_secs_f32() * 1000.0;
            if let Some(_guard) = bridge.memory.lock(false) {
                unsafe {
                    bridge.memory.write(
                        PHASES + (bridge.timing.sequence as usize % TIMING_COUNT) * 32,
                        phases,
                    );
                }
            }
        }
        if let Some(exit) = app.should_exit() {
            return exit;
        }
    }
}
