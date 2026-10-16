//! Two persistent GPU readback buffers; no screenshot entities or per-frame
//! GPU allocations. The rendered image includes the genuine weapon and HUD.
use super::{Bridge, FRAME, GetTickCount, MAX_HEIGHT, MAX_WIDTH, Memory, PIXELS, host_has_player};
use bevy::prelude::*;
use bevy::render::render_resource::{
    Buffer, BufferDescriptor, BufferUsages, CommandEncoderDescriptor, MapMode, TexelCopyBufferInfo,
    TexelCopyBufferLayout, TextureFormat,
};
use bevy::render::renderer::{RenderDevice, RenderGraph, RenderGraphSystems, RenderQueue};
use bevy::render::texture::GpuImage;
use bevy::render::{Extract, ExtractSchedule, RenderApp, render_asset::RenderAssets};
use std::sync::{
    Arc,
    atomic::{AtomicBool, AtomicU32, Ordering},
};
use std::time::{Duration, Instant};

#[derive(Default)]
pub(super) struct Status {
    pub pending: AtomicU32,
    pub latency: AtomicU32,
}
struct Slot {
    buffer: Buffer,
    busy: Arc<AtomicBool>,
}
#[derive(Resource)]
struct Capture {
    memory: Arc<Memory>,
    status: Arc<Status>,
    target: Option<Handle<Image>>,
    live: bool,
    slots: Vec<Slot>,
    size: UVec2,
    sequence: Option<u32>,
    last_capture: Instant,
}
pub(super) fn install(app: &mut App, memory: Arc<Memory>, status: Arc<Status>) {
    let Some(render) = app.get_sub_app_mut(RenderApp) else {
        return;
    };
    render.insert_resource(Capture {
        memory,
        status,
        target: None,
        live: false,
        slots: Vec::new(),
        size: UVec2::ZERO,
        sequence: None,
        last_capture: Instant::now(),
    });
    render.add_systems(ExtractSchedule, extract);
    render.add_systems(
        RenderGraph,
        copy_frame
            .in_set(RenderGraphSystems::Finish)
            .after(RenderGraphSystems::Submit),
    );
}
fn extract(bridge: Extract<Res<Bridge>>, mut capture: ResMut<Capture>) {
    capture.target = bridge.overlay.clone();
    capture.live = bridge.guest.ready != 0 && host_has_player(&bridge);
}
fn copy_frame(
    mut capture: ResMut<Capture>,
    images: Res<RenderAssets<GpuImage>>,
    device: Res<RenderDevice>,
    queue: Res<RenderQueue>,
) {
    if !capture.live || capture.last_capture.elapsed() < Duration::from_millis(14) {
        return;
    }
    let Some(image) = capture
        .target
        .as_ref()
        .and_then(|target| images.get(target))
    else {
        return;
    };
    let extent = image.texture_descriptor.size;
    let size = UVec2::new(extent.width, extent.height);
    if size.x == 0
        || size.y == 0
        || size.x > MAX_WIDTH
        || size.y > MAX_HEIGHT
        || !matches!(
            image.texture.format(),
            TextureFormat::Bgra8Unorm | TextureFormat::Bgra8UnormSrgb
        )
    {
        return;
    }
    let row = size.x * 4;
    let stride = row.div_ceil(256) * 256;
    if capture.size != size {
        if capture.status.pending.load(Ordering::Acquire) != 0 {
            return;
        }
        capture.slots = (0..2)
            .map(|_| Slot {
                buffer: device.create_buffer(&BufferDescriptor {
                    label: Some("GTA persistent readback"),
                    size: u64::from(stride) * u64::from(size.y),
                    usage: BufferUsages::COPY_DST | BufferUsages::MAP_READ,
                    mapped_at_creation: false,
                }),
                busy: Arc::new(AtomicBool::new(false)),
            })
            .collect();
        capture.size = size;
    }
    let Some(slot) = capture
        .slots
        .iter()
        .position(|slot| !slot.busy.load(Ordering::Acquire))
    else {
        return;
    };
    if capture.sequence.is_none() {
        let memory = capture.memory.clone();
        let Some(_guard) = memory.lock(true) else {
            return;
        };
        // A mapping survives helper restart: continue the previous image order.
        capture.sequence = Some(unsafe { memory.read::<u32>(FRAME + 16) });
    }
    let sequence = capture.sequence.unwrap().wrapping_add(1);
    capture.sequence = Some(sequence);
    capture.last_capture = Instant::now();
    let tick = unsafe { GetTickCount() };
    let buffer = capture.slots[slot].buffer.clone();
    let busy = capture.slots[slot].busy.clone();
    busy.store(true, Ordering::Release);
    capture.status.pending.fetch_add(1, Ordering::AcqRel);
    let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor {
        label: Some("GTA image readback"),
    });
    encoder.copy_texture_to_buffer(
        image.texture.as_image_copy(),
        TexelCopyBufferInfo {
            buffer: &buffer,
            layout: TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(stride),
                rows_per_image: Some(size.y),
            },
        },
        extent,
    );
    queue.submit([encoder.finish()]);
    let memory = capture.memory.clone();
    let status = capture.status.clone();
    let mapped = buffer.clone();
    buffer.slice(..).map_async(MapMode::Read, move |result| {
        if result.is_ok() {
            if let Ok(bytes) = mapped.slice(..).get_mapped_range() {
                if let Some(_guard) = memory.lock(true) {
                    unsafe {
                        if sequence.wrapping_sub(memory.read::<u32>(FRAME + 16)) < 0x8000_0000 {
                            let destination = std::slice::from_raw_parts_mut(
                                (memory.address + PIXELS) as *mut u8,
                                (row * size.y) as usize,
                            );
                            for (source, target) in bytes
                                .chunks_exact(stride as usize)
                                .zip(destination.chunks_exact_mut(row as usize))
                            {
                                target.copy_from_slice(&source[..row as usize]);
                            }
                            memory.write(FRAME + 4, size.x);
                            memory.write(FRAME + 8, size.y);
                            memory.write(FRAME + 12, size.x * size.y);
                            memory.write(FRAME + 16, sequence);
                            memory.write(FRAME + 20, tick);
                            memory.write(FRAME, memory.read::<u32>(FRAME).wrapping_add(1));
                        }
                    }
                }
                drop(bytes);
            }
            mapped.unmap();
        }
        status.latency.store(
            unsafe { GetTickCount() }.wrapping_sub(tick),
            Ordering::Release,
        );
        status.pending.fetch_sub(1, Ordering::AcqRel);
        busy.store(false, Ordering::Release);
    });
}
