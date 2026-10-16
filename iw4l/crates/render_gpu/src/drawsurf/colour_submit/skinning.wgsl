struct Vertex {
    packed: array<u32, 8>,
    bones: vec4<u32>,
    weights: vec4<f32>,
}
struct Params {
    destination: u32,
    count: u32,
    pad0: u32,
    pad1: u32,
    offset: vec4<f32>,
}
@group(0) @binding(0) var<storage, read> vertices: array<Vertex>;
@group(0) @binding(1) var<storage, read> bones: array<mat4x4<f32>>;
@group(0) @binding(2) var<uniform> params: Params;
@group(0) @binding(3) var<storage, read_write> output: array<u32>;

fn direction(word: u32) -> vec3<f32> {
    let bytes = vec3<f32>(f32(word & 255u), f32((word >> 8u) & 255u), f32((word >> 16u) & 255u));
    return (bytes - vec3<f32>(127.0)) * ((f32(word >> 24u) + 192.0) / 32385.0);
}
fn pack_direction(value: vec3<f32>) -> u32 {
    let bytes = vec3<u32>(clamp(value * 127.0 + vec3<f32>(127.5), vec3<f32>(0.0), vec3<f32>(255.0)));
    return bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (63u << 24u);
}
fn point(matrix: mat4x4<f32>, p: vec3<f32>) -> vec3<f32> {
    return p.x * matrix[0].xyz + p.y * matrix[1].xyz + p.z * matrix[2].xyz + matrix[3].xyz;
}
@compute @workgroup_size(64)
fn skin_main(@builtin(global_invocation_id) id: vec3<u32>) {
    if id.x >= params.count { return; }
    let vertex = vertices[id.x];
    let base = (params.destination + id.x) * 8u;
    for (var word = 0u; word < 8u; word += 1u) {
        output[base + word] = vertex.packed[word];
    }
    let source = vec3<f32>(bitcast<f32>(vertex.packed[0]), bitcast<f32>(vertex.packed[1]), bitcast<f32>(vertex.packed[2]));
    var position = source;
    if vertex.bones.x != 0xffffffffu {
        let primary = bones[vertex.bones.x];
        var sum = 0.0;
        var extra = vec3<f32>(0.0);
        for (var k = 1u; k < 4u; k += 1u) {
            let weight = vertex.weights[k];
            if weight > 0.0 {
                sum += weight;
                extra += weight * point(bones[vertex.bones[k]], source);
            }
        }
        position = (1.0 - sum) * point(primary, source) + extra;
        if all(position == vec3<f32>(0.0)) && vertex.weights.x != 0.0 {
            position = source;
        }
        for (var word = 6u; word < 8u; word += 1u) {
            output[base + word] = pack_direction((primary * vec4<f32>(direction(vertex.packed[word]), 0.0)).xyz);
        }
    }
    position += params.offset.xyz;
    output[base] = bitcast<u32>(position.x);
    output[base + 1u] = bitcast<u32>(position.y);
    output[base + 2u] = bitcast<u32>(position.z);
}
