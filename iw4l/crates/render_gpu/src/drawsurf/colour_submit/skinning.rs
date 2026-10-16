use std::collections::{HashMap, HashSet};

use bevy::prelude::Resource;
use bevy::render::render_resource::*;
use bevy::render::renderer::{PendingCommandBuffers, RenderDevice, RenderQueue};
use render_frame::skinning::PackedSkinJob;

use super::residency::GpuStream;

struct Pipeline {
    layout: BindGroupLayout,
    pipeline: ComputePipeline,
}

#[derive(Default)]
struct JobBuffers {
    bones: GpuStream,
    params: GpuStream,
}

#[derive(Resource, Default)]
pub(super) struct SkinGpu {
    pipeline: Option<Pipeline>,
    shapes: HashMap<u64, Buffer>,
    jobs: Vec<JobBuffers>,
}

impl SkinGpu {
    pub(super) fn resident_bytes(&self) -> u64 {
        self.shapes
            .values()
            .map(|buffer| buffer.size())
            .sum::<u64>()
            + self
                .jobs
                .iter()
                .flat_map(|job| [job.bones.buffer(), job.params.buffer()])
                .flatten()
                .map(|buffer| buffer.size())
                .sum::<u64>()
    }

    pub(super) fn dispatch(
        &mut self,
        jobs: &[PackedSkinJob],
        output: Option<&Buffer>,
        vertex_count: usize,
        device: &RenderDevice,
        queue: &RenderQueue,
        pending: &mut PendingCommandBuffers,
    ) -> bool {
        let _prepare = perf::Span::RenderSkinPrepareMs.enter();
        let live: HashSet<_> = jobs.iter().map(|job| job.shape.id()).collect();
        self.shapes.retain(|id, _| live.contains(id));
        self.jobs.truncate(jobs.len());
        if jobs.is_empty() {
            return true;
        }
        let Some(output) = output else {
            return false;
        };
        let limits = device.limits();
        if vertex_count.saturating_mul(32) as u64 > output.size()
            || output.size() > u64::from(limits.max_storage_buffer_binding_size)
        {
            return false;
        }
        let mut ranges = Vec::with_capacity(jobs.len());
        for job in jobs {
            let count = job.shape.vertices().len();
            let Some(end) = (job.destination as usize).checked_add(count) else {
                return false;
            };
            if count == 0
                || end > vertex_count
                || count.div_ceil(64) > limits.max_compute_workgroups_per_dimension as usize
                || count.saturating_mul(64) > limits.max_storage_buffer_binding_size as usize
                || job.bones.is_empty()
                || job.bones.len().saturating_mul(64)
                    > limits.max_storage_buffer_binding_size as usize
                || job.shape.required_bones() > job.bones.len()
            {
                return false;
            }
            ranges.push((job.destination as usize, end));
        }
        ranges.sort_unstable();
        if ranges.windows(2).any(|pair| pair[0].1 > pair[1].0) {
            return false;
        }
        let pipeline = self.pipeline.get_or_insert_with(|| make_pipeline(device));
        let mut encoder = device.create_command_encoder(&CommandEncoderDescriptor {
            label: Some("packed_skin"),
        });
        self.jobs.resize_with(jobs.len(), JobBuffers::default);
        let mut bindings = Vec::with_capacity(jobs.len());
        for (job, buffers) in jobs.iter().zip(&mut self.jobs) {
            let shape = self.shapes.entry(job.shape.id()).or_insert_with(|| {
                device.create_buffer_with_data(&BufferInitDescriptor {
                    label: Some("packed_skin_shape"),
                    contents: bytemuck::cast_slice(job.shape.vertices()),
                    usage: BufferUsages::STORAGE,
                })
            });
            buffers.bones.reserve(
                device,
                "packed_skin_bones",
                BufferUsages::STORAGE,
                job.bones.len(),
                64,
            );
            buffers
                .params
                .reserve(device, "packed_skin_params", BufferUsages::UNIFORM, 1, 32);
            let params = [
                job.destination,
                job.shape.vertices().len() as u32,
                0,
                0,
                job.offset[0].to_bits(),
                job.offset[1].to_bits(),
                job.offset[2].to_bits(),
                0,
            ];
            if !buffers
                .bones
                .write_at(queue, 0, bytemuck::cast_slice(&job.bones))
                || !buffers
                    .params
                    .write_at(queue, 0, bytemuck::cast_slice(&params))
            {
                return false;
            }
            bindings.push(device.create_bind_group(
                "packed_skin",
                &pipeline.layout,
                &BindGroupEntries::sequential((
                    shape.as_entire_binding(),
                    buffers.bones.buffer().unwrap().as_entire_binding(),
                    buffers.params.buffer().unwrap().as_entire_binding(),
                    output.as_entire_binding(),
                )),
            ));
        }
        {
            let mut pass = encoder.begin_compute_pass(&ComputePassDescriptor {
                label: Some("packed_skin"),
                timestamp_writes: None,
            });
            pass.set_pipeline(&pipeline.pipeline);
            for (job, binding) in jobs.iter().zip(&bindings) {
                pass.set_bind_group(0, &**binding, &[]);
                pass.dispatch_workgroups((job.shape.vertices().len() as u32).div_ceil(64), 1, 1);
            }
        }
        // Pending buffers precede the render graph's colour and shadow consumers.
        pending.push([encoder.finish()]);
        true
    }
}

fn make_pipeline(device: &RenderDevice) -> Pipeline {
    let storage = |binding, read_only| BindGroupLayoutEntry {
        binding,
        visibility: ShaderStages::COMPUTE,
        ty: BindingType::Buffer {
            ty: BufferBindingType::Storage { read_only },
            has_dynamic_offset: false,
            min_binding_size: None,
        },
        count: None,
    };
    let layout = device.create_bind_group_layout(
        "packed_skin",
        &[
            storage(0, true),
            storage(1, true),
            BindGroupLayoutEntry {
                binding: 2,
                visibility: ShaderStages::COMPUTE,
                ty: BindingType::Buffer {
                    ty: BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            },
            storage(3, false),
        ],
    );
    let module = device.create_and_validate_shader_module(ShaderModuleDescriptor {
        label: Some("packed_skin"),
        source: ShaderSource::Wgsl(include_str!("skinning.wgsl").into()),
    });
    let pipeline_layout = device.create_pipeline_layout(&PipelineLayoutDescriptor {
        label: Some("packed_skin"),
        bind_group_layouts: &[Some(&*layout)],
        immediate_size: 0,
    });
    let pipeline = device.create_compute_pipeline(&RawComputePipelineDescriptor {
        label: Some("packed_skin"),
        layout: Some(&pipeline_layout),
        module: &module,
        entry_point: Some("skin_main"),
        compilation_options: Default::default(),
        cache: None,
    });
    Pipeline { layout, pipeline }
}
