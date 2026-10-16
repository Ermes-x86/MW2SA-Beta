use std::sync::{
    Arc,
    atomic::{AtomicU64, Ordering},
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PackedSkinVertex {
    pub packed: [u32; 8],
    pub bones: [u32; 4],
    pub weights: [f32; 4],
}

#[derive(Debug)]
pub struct PackedSkinShape {
    id: u64,
    vertices: Vec<PackedSkinVertex>,
    required_bones: usize,
}

impl PackedSkinShape {
    pub fn new(vertices: Vec<PackedSkinVertex>) -> Arc<Self> {
        static NEXT: AtomicU64 = AtomicU64::new(1);
        let id = NEXT
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .expect("skin shape identity exhausted");
        let required_bones = vertices
            .iter()
            .filter(|vertex| vertex.bones[0] != u32::MAX)
            .flat_map(|vertex| {
                std::iter::once(vertex.bones[0]).chain(
                    (1..4)
                        .filter(|&extra| vertex.weights[extra] > 0.0)
                        .map(|extra| vertex.bones[extra]),
                )
            })
            .max()
            .map_or(0, |bone| bone as usize + 1);
        Arc::new(Self {
            id,
            vertices,
            required_bones,
        })
    }

    pub fn id(&self) -> u64 {
        self.id
    }
    pub fn required_bones(&self) -> usize {
        self.required_bones
    }
    pub fn vertices(&self) -> &[PackedSkinVertex] {
        &self.vertices
    }
}

#[derive(Clone, Debug)]
pub struct PackedSkinJob {
    pub shape: Arc<PackedSkinShape>,
    pub bones: Vec<[[f32; 4]; 4]>,
    pub destination: u32,
    pub offset: [f32; 3],
}
