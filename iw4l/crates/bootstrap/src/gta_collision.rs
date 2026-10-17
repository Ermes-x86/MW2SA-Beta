//! GTA collision surfaces adapted to IW4's existing capsule sweep. No movement
//! acceleration, friction, sprint, jump or stance rules are implemented here.
use movement_iw4::penetration::{Capsule, ContactResult, Plane, Vec3, capsule_plane_hull};
use movement_iw4::recovery::{ContactBuffer, Coverage};
use movement_iw4::{CollisionBackend, GroundTraceInput};
use trace_iw4::{BrushRef, Trace};

pub const UNITS_PER_METER: f32 = 39.37008;
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct HostTriangle {
    pub vertices: [[f32; 3]; 3],
    pub surface: u32,
}
const _: () = assert!(std::mem::size_of::<HostTriangle>() == 40);
#[derive(Debug)]
struct Surface {
    /// Face, back face and three edge planes first, then bevels; only the
    /// first `count` are used.
    planes: [[f32; 4]; MAX_PLANES],
    count: usize,
    mins: [f32; 3],
    maxs: [f32; 3],
    /// Host entity identity for movable vehicles, peds and props; 0 is static.
    tag: u32,
}
/// What `release_traps` did to free a player IW4's recovery could not.
#[derive(Debug, Default, PartialEq)]
pub struct TrapRelease {
    /// Surfaces left out of this scene (released entities and walls).
    pub surfaces: usize,
    /// Of those, steep static faces released because no lift was clear.
    pub walls: usize,
    /// Upward move, in IW4 units, that clears the player's hull.
    pub lift: Option<f32>,
}
/// Largest upward move used to free a player, about one metre.
const MAX_LIFT: f32 = 40.0;
/// Largest step used to free a blocked player, about 0.6 m: above IW4's own
/// 18-unit step, below anything a player could not climb in MW2 by mantling.
const MAX_UNBLOCK_STEP: f32 = 24.0;
/// How far ahead a lifted player must have open space, in IW4 units.
const UNBLOCK_AHEAD: f32 = 16.0;
/// What blocks a player who pushes against host geometry without moving.
#[derive(Debug)]
pub struct MoveProbe {
    pub fraction: f32,
    pub normal: [f32; 3],
    pub startsolid: bool,
}
#[derive(Debug, Default)]
pub struct HostCollision {
    surfaces: Vec<Surface>,
}
fn subtract(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    std::array::from_fn(|i| a[i] - b[i])
}
fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
/// The capsule used by the genuine IW4 sweep for these bounds.
fn player_capsule(input: GroundTraceInput) -> Capsule {
    let offset: [f32; 3] = std::array::from_fn(|i| (input.mins[i] + input.maxs[i]) * 0.5);
    let half: [f32; 3] = std::array::from_fn(|i| input.maxs[i] - offset[i]);
    let radius = half[0].min(half[2]);
    let center = Vec3::from(std::array::from_fn(|i| {
        f64::from(input.start[i] + offset[i])
    }));
    let segment = Vec3::new(0.0, 0.0, f64::from(half[2] - radius));
    Capsule {
        a: center - segment,
        b: center + segment,
        radius: f64::from(radius),
    }
}
fn hull_contact(cap: Capsule, surface: &Surface) -> ContactResult {
    capsule_plane_hull(
        cap,
        surface.planes().iter().map(|p| Plane {
            normal: Vec3::from([f64::from(p[0]), f64::from(p[1]), f64::from(p[2])]),
            d: f64::from(p[3]),
        }),
    )
}
fn normalized(v: [f32; 3]) -> Option<[f32; 3]> {
    let length = dot(v, v).sqrt();
    (length.is_finite() && length > 0.001).then(|| v.map(|c| c / length))
}
/// The prism's own five planes, then at most six axial bevels and two per
/// edge and axis.
const MAX_PLANES: usize = 5 + 6 + 3 * 3 * 2;
impl Surface {
    fn planes(&self) -> &[[f32; 4]] {
        &self.planes[..self.count]
    }
}
/// Adds the bevel planes IW4's map compiler gives every brush. The capsule
/// sweep pushes each plane out by the capsule's extent along its normal;
/// without bevels, faces meeting at an angle push the brush out past its own
/// edges and corners, so a player walking from a stair ramp onto its landing
/// or over a slope change between road models runs into solid that is not
/// there. Each bevel is a plane through the prism's farthest point in its
/// direction, so it never removes genuine solid, only those overhangs.
fn add_bevels(surface: &mut Surface, triangle: [[f32; 3]; 3], normal: [f32; 3]) {
    let mut corners = [[0.0; 3]; 6];
    for (i, v) in triangle.into_iter().enumerate() {
        corners[i] = std::array::from_fn(|k| v[k] + normal[k] * 0.02);
        corners[i + 3] = std::array::from_fn(|k| v[k] - normal[k] * 0.02);
    }
    let axes = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]];
    let [a, b, c] = triangle;
    let edges = [subtract(b, a), subtract(c, b), subtract(a, c)];
    let directions = axes
        .into_iter()
        .map(Some)
        .chain(edges.into_iter().flat_map(|edge| axes.map(|axis| normalized(cross(edge, axis)))))
        .flatten()
        .flat_map(|n: [f32; 3]| [n, n.map(|c| -c)]);
    for n in directions {
        // A plane the brush already has, or nearly so, adds nothing.
        if surface.planes().iter().any(|p| dot(n, [p[0], p[1], p[2]]) > 0.9999) {
            continue;
        }
        let farthest = corners
            .iter()
            .map(|&v| dot(n, v))
            .fold(f32::NEG_INFINITY, f32::max);
        surface.planes[surface.count] = [n[0], n[1], n[2], farthest];
        surface.count += 1;
    }
}
impl HostCollision {
    #[cfg(test)]
    pub fn from_triangles(triangles: &[HostTriangle], anchor: [f32; 3]) -> Self {
        Self::from_triangles_at(triangles, anchor, [0.0; 3])
    }
    pub fn from_triangles_at(
        triangles: &[HostTriangle],
        anchor: [f32; 3],
        engine_anchor: [f32; 3],
    ) -> Self {
        let mut surfaces = Vec::with_capacity(triangles.len());
        for triangle in triangles {
            let vertices = triangle.vertices.map(|v| {
                std::array::from_fn(|i| (v[i] - anchor[i]) * UNITS_PER_METER + engine_anchor[i])
            });
            if !vertices.iter().flatten().all(|c| c.is_finite()) {
                continue;
            }
            let [a, b, c] = vertices;
            let Some(normal) = normalized(cross(subtract(b, a), subtract(c, a))) else {
                continue;
            };
            let mins = std::array::from_fn(|i| a[i].min(b[i]).min(c[i]) - 0.03);
            let maxs = std::array::from_fn(|i| a[i].max(b[i]).max(c[i]) + 0.03);
            // Material mapping currently uses generic solid. GTA retains the
            // actual surface material for bullet impacts and world responses.
            let mut surface = Surface {
                planes: [[0.0; 4]; MAX_PLANES],
                count: 5,
                mins,
                maxs,
                tag: triangle.surface >> 8,
            };
            let distance = dot(normal, a);
            // A thin closed prism supplies both sides of each genuine GTA face
            // to the existing IW4 brush/capsule trace implementation.
            surface.planes[0] = [normal[0], normal[1], normal[2], distance + 0.02];
            surface.planes[1] = [-normal[0], -normal[1], -normal[2], -distance + 0.02];
            let mut valid = true;
            for (i, (start, end)) in [(a, b), (b, c), (c, a)].into_iter().enumerate() {
                let Some(outward) = normalized(cross(subtract(end, start), normal)) else {
                    valid = false;
                    break;
                };
                surface.planes[i + 2] = [outward[0], outward[1], outward[2], dot(outward, start)];
            }
            if !valid {
                continue;
            }
            add_bevels(&mut surface, [a, b, c], normal);
            surfaces.push(surface);
        }
        Self { surfaces }
    }
    pub fn is_empty(&self) -> bool {
        self.surfaces.is_empty()
    }
    /// Frees a player whom IW4's bounded recovery cannot. Host vehicles, peds
    /// and props (a car driving into the player, a door dropped by an
    /// explosion) can enter the capsule deeper than the solver's budget
    /// between scene updates; the whole entity then stops blocking until the
    /// capsule has left its bounds. Static geometry can do the same where
    /// models meet or a detailed collision model streams in around the player
    /// (stairs, road seams): such a player is lifted by the smallest step that
    /// clears the hull and falls back onto the surface. Only when no lift up to
    /// `MAX_LIFT` is clear are steep static faces released for this scene;
    /// floors always stay. Contacts IW4 resolves itself are untouched.
    pub fn release_traps(
        &mut self,
        origin: [f32; 3],
        mins: [f32; 3],
        maxs: [f32; 3],
        tracemask: u32,
        ghosts: &mut Vec<u32>,
    ) -> TrapRelease {
        let before = self.surfaces.len();
        let mut walls = 0;
        let lo: [f32; 3] = std::array::from_fn(|i| origin[i] + mins[i] - 1.0);
        let hi: [f32; 3] = std::array::from_fn(|i| origin[i] + maxs[i] + 1.0);
        ghosts.retain(|&tag| {
            let mut bounds: Option<([f32; 3], [f32; 3])> = None;
            for s in self.surfaces.iter().filter(|s| s.tag == tag) {
                let (a, b) = bounds.get_or_insert((s.mins, s.maxs));
                for i in 0..3 {
                    a[i] = a[i].min(s.mins[i]);
                    b[i] = b[i].max(s.maxs[i]);
                }
            }
            bounds.is_some_and(|(a, b)| (0..3).all(|i| a[i] <= hi[i] && b[i] >= lo[i]))
        });
        // A vehicle that passed fully around the capsule between scene updates
        // leaves the player enclosed in its hollow shell without any contact.
        // Treat a movable entity with faces both over and under the player's
        // footprint as such an enclosure. Truck beds and wings lack one side.
        let mut over = Vec::new();
        let mut under = Vec::new();
        for s in &self.surfaces {
            if s.tag == 0
                || !(0..2)
                    .all(|i| s.mins[i] <= origin[i] + maxs[i] && s.maxs[i] >= origin[i] + mins[i])
            {
                continue;
            }
            if s.mins[2] >= origin[2] + maxs[2] - 1.0 && !over.contains(&s.tag) {
                over.push(s.tag);
            }
            if s.maxs[2] <= origin[2] + 2.0 && !under.contains(&s.tag) {
                under.push(s.tag);
            }
        }
        for tag in over {
            if under.contains(&tag) && !ghosts.contains(&tag) {
                ghosts.push(tag);
            }
        }
        self.surfaces
            .retain(|s| s.tag == 0 || !ghosts.contains(&s.tag));
        let input = GroundTraceInput {
            start: origin,
            end: origin,
            mins,
            maxs,
            tracemask,
        };
        let trapped = |scene: &Self| {
            let trace = scene.trace(input);
            (trace.startsolid != 0 || trace.allsolid != 0)
                && scene.correct_solid(origin, mins, maxs, tracemask).is_none()
        };
        for pass in 0..2 {
            if !trapped(self) {
                break;
            }
            if pass == 1 {
                let lift = (1..=MAX_LIFT as i32).map(|dz| dz as f32).find(|&dz| {
                    let at = [origin[0], origin[1], origin[2] + dz];
                    let trace = self.trace(GroundTraceInput {
                        start: at,
                        end: at,
                        ..input
                    });
                    trace.startsolid == 0 && trace.allsolid == 0
                });
                if lift.is_some() {
                    return TrapRelease {
                        surfaces: before - self.surfaces.len(),
                        walls: 0,
                        lift,
                    };
                }
            }
            let touching: Vec<usize> = (0..self.surfaces.len())
                .filter(|&i| self.touches(i, input))
                .collect();
            // Descending order: swap_remove only moves already-visited entries.
            for i in touching.into_iter().rev() {
                let s = &self.surfaces[i];
                if s.tag != 0 {
                    if !ghosts.contains(&s.tag) {
                        ghosts.push(s.tag);
                    }
                } else if pass == 1 && s.planes[0][2].abs() < 0.7 {
                    self.surfaces.swap_remove(i);
                    walls += 1;
                }
            }
            self.surfaces
                .retain(|s| s.tag == 0 || !ghosts.contains(&s.tag));
        }
        TrapRelease {
            surfaces: before - self.surfaces.len(),
            walls,
            lift: None,
        }
    }
    /// For a player pushing in `direction` (horizontal, unit length) without
    /// moving: the smallest lift that clears the hull and opens space ahead,
    /// e.g. a kerb, stair lip or seam between GTA models that IW4's step does
    /// not climb. `Err` describes what blocks the move when no such step
    /// exists, or reports an open way when geometry is not what blocks it.
    pub fn unblock_lift(
        &self,
        origin: [f32; 3],
        mins: [f32; 3],
        maxs: [f32; 3],
        tracemask: u32,
        direction: [f32; 2],
    ) -> Result<f32, MoveProbe> {
        let probe = |at: [f32; 3]| {
            self.trace(GroundTraceInput {
                start: at,
                end: [
                    at[0] + direction[0] * UNBLOCK_AHEAD,
                    at[1] + direction[1] * UNBLOCK_AHEAD,
                    at[2],
                ],
                mins,
                maxs,
                tracemask,
            })
        };
        let here = probe(origin);
        let blocked = MoveProbe {
            fraction: here.fraction,
            normal: here.normal,
            startsolid: here.startsolid != 0 || here.allsolid != 0,
        };
        if !blocked.startsolid && blocked.fraction >= 0.5 {
            return Err(blocked);
        }
        (2..=MAX_UNBLOCK_STEP as i32)
            .step_by(2)
            .map(|dz| dz as f32)
            .find(|&dz| {
                let at = [origin[0], origin[1], origin[2] + dz];
                let clear = self.trace(GroundTraceInput {
                    start: at,
                    end: at,
                    mins,
                    maxs,
                    tracemask,
                });
                let ahead = probe(at);
                clear.startsolid == 0
                    && clear.allsolid == 0
                    && ahead.startsolid == 0
                    && ahead.fraction >= 0.5
            })
            .ok_or(blocked)
    }
    fn touches(&self, index: usize, input: GroundTraceInput) -> bool {
        matches!(
            hull_contact(player_capsule(input), &self.surfaces[index]),
            ContactResult::Contact(_)
        )
    }
}
impl CollisionBackend for HostCollision {
    fn penetrations(&self, input: GroundTraceInput, contacts: &mut ContactBuffer) -> Coverage {
        if !input
            .start
            .iter()
            .chain(input.mins.iter())
            .chain(input.maxs.iter())
            .all(|x| x.is_finite())
            || (0..3).any(|i| input.mins[i] > input.maxs[i])
        {
            return Coverage::Unsupported;
        }
        if input.tracemask & 1 == 0 {
            return Coverage::Complete;
        }
        // Match the capsule bounds used by the genuine IW4 sweep. Feeding its
        // existing recovery solver allows small scene overlaps to be resolved
        // without inventing a separate movement or teleport routine.
        let cap = player_capsule(input);
        let mins: [f32; 3] = std::array::from_fn(|i| input.start[i] + input.mins[i] - 0.25);
        let maxs: [f32; 3] = std::array::from_fn(|i| input.start[i] + input.maxs[i] + 0.25);
        for surface in &self.surfaces {
            if !(0..3).all(|i| surface.mins[i] <= maxs[i] && surface.maxs[i] >= mins[i]) {
                continue;
            }
            match hull_contact(cap, surface) {
                ContactResult::Clear => {}
                ContactResult::Contact(contact) => {
                    // Adjacent triangles often supply the same supporting plane.
                    // Deduplicate it before the solver's bounded contact buffer.
                    if contacts.as_slice().iter().any(|c| {
                        (c.normal - contact.normal).length_squared() < 1e-12
                            && (c.depth - contact.depth).abs() < 1e-6
                    }) {
                        continue;
                    }
                    if !contacts.push(contact) {
                        return Coverage::Overflow;
                    }
                }
                ContactResult::Invalid | ContactResult::Unsupported => {
                    return Coverage::Unsupported;
                }
            }
        }
        Coverage::Complete
    }
    fn trace(&self, input: GroundTraceInput) -> Trace {
        let mins: [f32; 3] =
            std::array::from_fn(|i| input.start[i].min(input.end[i]) + input.mins[i] - 0.25);
        let maxs: [f32; 3] =
            std::array::from_fn(|i| input.start[i].max(input.end[i]) + input.maxs[i] + 0.25);
        trace_iw4::trace_capsule(
            self.surfaces
                .iter()
                .filter(|surface| {
                    (0..3).all(|i| surface.mins[i] <= maxs[i] && surface.maxs[i] >= mins[i])
                })
                .map(|surface| BrushRef {
                    planes: surface.planes(),
                    contents: 1,
                    plane_surface_flags: &[],
                    glass_encoded: 0,
                }),
            input.start,
            input.end,
            input.mins,
            input.maxs,
            input.tracemask,
        )
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shallow_floor_and_corner_overlaps_recover_without_a_large_warp() {
        let scene = HostCollision::from_triangles(
            &[
                HostTriangle {
                    vertices: [[-20.0, -20.0, 0.0], [20.0, -20.0, 0.0], [0.0, 20.0, 0.0]],
                    surface: 0,
                },
                HostTriangle {
                    vertices: [[2.54, -20.0, -20.0], [2.54, 20.0, -20.0], [2.54, 0.0, 20.0]],
                    surface: 0,
                },
            ],
            [0.0; 3],
        );
        for start in [[0.0, 0.0, -1.0], [86.0, 0.0, -1.0]] {
            assert_ne!(trace(&scene, start, start).allsolid, 0);
            let corrected = scene
                .correct_solid(start, [-15.0, -15.0, 0.0], [15.0, 15.0, 70.0], 0x02810011)
                .expect("IW4 must recover shallow host overlap");
            let clear = trace(&scene, corrected.origin, corrected.origin);
            assert_eq!((clear.startsolid, clear.allsolid), (0, 0), "{corrected:?}");
            assert!(corrected.origin[2] >= 0.0 && corrected.origin[2] < 1.0);
            assert!((corrected.origin[0] - start[0]).abs() < 2.0);
        }
        // The solver refuses to move through a wall or warp out of deep solids.
        assert!(
            scene
                .correct_solid(
                    [100.0, 0.0, 1.0],
                    [-15.0, -15.0, 0.0],
                    [15.0, 15.0, 70.0],
                    0x02810011
                )
                .is_none()
        );
    }
    fn host_box(min: [f32; 3], max: [f32; 3], tag: u32) -> Vec<HostTriangle> {
        const FACES: [[usize; 3]; 12] = [
            [0, 1, 3],
            [0, 3, 2],
            [4, 6, 7],
            [4, 7, 5],
            [0, 4, 5],
            [0, 5, 1],
            [2, 3, 7],
            [2, 7, 6],
            [0, 2, 6],
            [0, 6, 4],
            [1, 5, 7],
            [1, 7, 3],
        ];
        let v: [[f32; 3]; 8] = std::array::from_fn(|j| {
            [
                if j & 1 != 0 { max[0] } else { min[0] },
                if j & 2 != 0 { max[1] } else { min[1] },
                if j & 4 != 0 { max[2] } else { min[2] },
            ]
        });
        FACES
            .iter()
            .map(|f| HostTriangle {
                vertices: [v[f[0]], v[f[1]], v[f[2]]],
                surface: tag << 8,
            })
            .collect()
    }
    fn release(scene: &mut HostCollision, origin: [f32; 3], ghosts: &mut Vec<u32>) -> usize {
        let result = scene.release_traps(
            origin,
            [-15.0, -15.0, 0.0],
            [15.0, 15.0, 70.0],
            0x02810011,
            ghosts,
        );
        assert_eq!(result.lift, None);
        result.surfaces
    }
    fn floor() -> HostTriangle {
        HostTriangle {
            vertices: [[-20.0, -20.0, 0.0], [20.0, -20.0, 0.0], [0.0, 20.0, 0.0]],
            surface: 0,
        }
    }
    #[test]
    fn vehicle_pushed_deep_into_player_is_released_but_floor_remains() {
        // A movable host entity overlapping the capsule by ~0.48 m, beyond the
        // solver's 8-unit recovery budget, as when a car or door hits CJ.
        let mut triangles = host_box([-0.1, -1.0, 0.2], [2.0, 1.0, 1.4], 5);
        triangles.push(floor());
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let origin = [0.0, 0.0, 0.5];
        assert_ne!(trace(&scene, origin, origin).startsolid, 0);
        assert!(
            scene
                .correct_solid(origin, [-15.0, -15.0, 0.0], [15.0, 15.0, 70.0], 0x02810011)
                .is_none()
        );
        let mut ghosts = Vec::new();
        assert_eq!(release(&mut scene, origin, &mut ghosts), 12);
        assert_eq!(ghosts, [5]);
        let clear = trace(&scene, origin, origin);
        assert_eq!((clear.startsolid, clear.allsolid), (0, 0));
        // The floor still supports the player.
        assert!(trace(&scene, origin, [0.0, 0.0, -10.0]).fraction < 1.0);

        // Rebuilt scenes keep the entity released while CJ is inside its bounds,
        // then restore it once the capsule has left.
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        assert_eq!(release(&mut scene, [20.0, 0.0, 0.5], &mut ghosts), 12);
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        assert_eq!(release(&mut scene, [-200.0, 0.0, 0.5], &mut ghosts), 0);
        assert!(ghosts.is_empty());
    }
    #[test]
    fn touching_or_standing_on_a_vehicle_keeps_it_solid() {
        let mut triangles = host_box([0.5, -1.0, 0.0], [2.0, 1.0, 1.4], 7);
        triangles.push(floor());
        let mut ghosts = Vec::new();
        // Beside the car, within normal contact clearance.
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        assert_eq!(release(&mut scene, [4.0, 0.0, 0.5], &mut ghosts), 0);
        // Standing on its roof.
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let roof = [1.25 * UNITS_PER_METER, 0.0, 1.4 * UNITS_PER_METER + 0.5];
        assert_eq!(release(&mut scene, roof, &mut ghosts), 0);
        assert!(ghosts.is_empty());
        assert!(trace(&scene, roof, [roof[0], roof[1], roof[2] - 10.0]).fraction < 1.0);
    }
    #[test]
    fn player_enclosed_in_a_vehicle_shell_is_released() {
        let mut triangles = host_box([-1.0, -1.0, 0.1], [1.0, 1.0, 2.2], 9);
        triangles.push(floor());
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let origin = [0.0, 0.0, 0.1 * UNITS_PER_METER + 0.5];
        assert_eq!(trace(&scene, origin, origin).startsolid, 0);
        let mut ghosts = Vec::new();
        assert_eq!(release(&mut scene, origin, &mut ghosts), 12);
        assert_eq!(ghosts, [9]);
    }
    #[test]
    fn player_sunk_into_a_stair_step_is_lifted_onto_it() {
        // A static step 0.3 m high whose top cuts through the capsule 12 units
        // above the feet, past IW4's 8-unit recovery: a seam between two
        // models, or stairs streaming in around the player.
        let mut triangles = host_box([-0.2, -1.0, 0.0], [2.0, 1.0, 0.3], 0);
        triangles.push(floor());
        let mut scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let origin = [0.0, 0.0, 0.0];
        assert!(
            scene
                .correct_solid(origin, [-15.0, -15.0, 0.0], [15.0, 15.0, 70.0], 0x02810011)
                .is_none()
        );
        let mut ghosts = Vec::new();
        let result = scene.release_traps(
            origin,
            [-15.0, -15.0, 0.0],
            [15.0, 15.0, 70.0],
            0x02810011,
            &mut ghosts,
        );
        // Lifted just above the step top (0.3 m = 11.8 units); nothing removed.
        let lift = result.lift.expect("a stuck player must be lifted clear");
        assert!(lift > 11.0 && lift < 14.0, "{lift}");
        assert_eq!((result.surfaces, ghosts.len()), (0, 0));
        let at = [0.0, 0.0, lift];
        let clear = trace(&scene, at, at);
        assert_eq!((clear.startsolid, clear.allsolid), (0, 0));
        // The step and the floor still support the player.
        assert!(trace(&scene, at, [0.0, 0.0, -10.0]).fraction < 1.0);
    }
    #[test]
    fn blocked_player_steps_onto_a_low_ledge_but_not_through_a_wall() {
        let unblock = |scene: &HostCollision, origin: [f32; 3]| {
            scene.unblock_lift(
                origin,
                [-15.0, -15.0, 0.0],
                [15.0, 15.0, 70.0],
                0x02810011,
                [1.0, 0.0],
            )
        };
        // A 0.5 m ledge (19.7 units), just above IW4's 18-unit step, right
        // in front of the capsule.
        let mut triangles = host_box([0.4, -2.0, 0.0], [3.0, 2.0, 0.5], 0);
        triangles.push(floor());
        let scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let origin = [0.0, 0.0, 0.25];
        let lift = unblock(&scene, origin).expect("a low ledge must be climbable");
        assert!(lift >= 19.0 && lift <= 24.0, "{lift}");
        // A 2 m wall stays a wall.
        let mut triangles = host_box([0.4, -2.0, 0.0], [3.0, 2.0, 2.0], 0);
        triangles.push(floor());
        let scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let wall = unblock(&scene, origin).expect_err("a wall must not be climbed");
        assert!(wall.fraction < 0.5 && wall.normal[0] < -0.9, "{wall:?}");
        // Nothing to step over in open space.
        let scene = HostCollision::from_triangles(&[floor()], [0.0; 3]);
        assert!(unblock(&scene, origin).unwrap_err().fraction >= 0.5);
    }
    /// Two triangles spanning the quad `p` (in metres, GTA coordinates).
    fn quad(p: [[f32; 3]; 4]) -> [HostTriangle; 2] {
        [
            HostTriangle {
                vertices: [p[0], p[1], p[2]],
                surface: 0,
            },
            HostTriangle {
                vertices: [p[0], p[2], p[3]],
                surface: 0,
            },
        ]
    }
    /// Walks a standing capsule along +x the way IW4's step-slide move does:
    /// a move blocked on the ground is retried 18 units higher and then put
    /// back down. Returns how far it got, in IW4 units, from `from` towards `to`.
    fn walk(scene: &HostCollision, from: f32, to: f32, height: f32) -> f32 {
        const STEP: f32 = 18.0;
        let down = |at: [f32; 3]| trace(scene, at, [at[0], at[1], at[2] - 3.0 * STEP]);
        let rest = down([from, 0.0, height]);
        assert_eq!(rest.startsolid, 0, "the walk must start in open space");
        let mut origin = rest.endpos;
        while origin[0] < to {
            let standing = trace(scene, origin, origin);
            assert_eq!(
                (standing.startsolid, standing.allsolid),
                (0, 0),
                "resting on the ground must not be solid at {origin:?}"
            );
            let ahead = [origin[0] + 2.0, origin[1], origin[2]];
            let flat = trace(scene, origin, ahead);
            let next = if flat.fraction == 1.0 {
                ahead
            } else {
                let up = [origin[0], origin[1], origin[2] + STEP];
                let raised = trace(scene, origin, up);
                let over = trace(
                    scene,
                    raised.endpos,
                    [raised.endpos[0] + 2.0, origin[1], raised.endpos[2]],
                );
                if over.startsolid != 0 || over.fraction == 0.0 {
                    return origin[0] - from;
                }
                over.endpos
            };
            let landed = down([next[0], next[1], next[2] + 1.0]);
            if landed.startsolid != 0 || landed.fraction == 1.0 || landed.endpos[0] <= origin[0] {
                return origin[0] - from;
            }
            origin = landed.endpos;
        }
        to - from
    }
    #[test]
    fn walking_up_and_down_ramp_stairs_does_not_snag_at_either_end() {
        // GTA stairs are usually one slope per flight: floor, a ramp 1.4 m
        // long, then a landing, each its own pair of triangles. IW4 walks
        // slopes up to about 45 degrees.
        for degrees in [25.0f32, 35.0, 42.0] {
            let rise = 1.4 * degrees.to_radians().tan();
            let mut triangles = Vec::new();
            triangles.extend(quad([[-3.0, -2.0, 0.0], [0.0, -2.0, 0.0], [0.0, 2.0, 0.0], [-3.0, 2.0, 0.0]]));
            triangles.extend(quad([[0.0, -2.0, 0.0], [1.4, -2.0, rise], [1.4, 2.0, rise], [0.0, 2.0, 0.0]]));
            triangles.extend(quad([[1.4, -2.0, rise], [4.0, -2.0, rise], [4.0, 2.0, rise], [1.4, 2.0, rise]]));
            let scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
            let up = walk(&scene, -2.0 * UNITS_PER_METER, 3.0 * UNITS_PER_METER, 30.0);
            assert!(up >= 5.0 * UNITS_PER_METER - 2.0, "{degrees} degrees: stuck going up after {up} units");
            let mirrored: Vec<HostTriangle> = triangles
                .iter()
                .map(|t| HostTriangle {
                    vertices: t.vertices.map(|v| [1.4 - v[0], v[1], v[2]]),
                    surface: 0,
                })
                .collect();
            let scene = HostCollision::from_triangles(&mirrored, [0.0; 3]);
            let top = rise * UNITS_PER_METER;
            let down = walk(&scene, -2.0 * UNITS_PER_METER, 3.0 * UNITS_PER_METER, top + 30.0);
            assert!(down >= 5.0 * UNITS_PER_METER - 2.0, "{degrees} degrees: stuck going down after {down} units");
        }
    }
    #[test]
    fn walking_up_block_steps_does_not_snag() {
        // Stairs modelled as one box per step: 0.18 m rise, 0.3 m tread.
        let mut triangles = quad([[-3.0, -2.0, 0.0], [6.0, -2.0, 0.0], [6.0, 2.0, 0.0], [-3.0, 2.0, 0.0]]).to_vec();
        for i in 0..8 {
            let x = i as f32 * 0.3;
            triangles.extend(host_box([x, -2.0, 0.0], [6.0, 2.0, 0.18 * (i + 1) as f32], 0));
        }
        let scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
        let walked = walk(&scene, -2.0 * UNITS_PER_METER, 4.0 * UNITS_PER_METER, 30.0);
        assert!(walked >= 6.0 * UNITS_PER_METER - 2.0, "stuck after {walked} units");
    }
    #[test]
    fn bevels_keep_walls_and_floors_solid() {
        // Walking into a wall at an angle still stops at the wall.
        let wall = HostTriangle {
            vertices: [[2.54, -20.0, -20.0], [2.54, 20.0, -20.0], [2.54, 0.0, 20.0]],
            surface: 0,
        };
        let scene = HostCollision::from_triangles(&[wall, floor()], [0.0; 3]);
        let hit = trace(&scene, [0.0, 0.0, 0.5], [200.0, 60.0, 0.5]);
        assert!(hit.fraction < 0.45 && hit.endpos[0] < 2.54 * UNITS_PER_METER - 14.0, "{hit:?}");
        // Seams between the two triangles of a floor never let the player drop.
        let scene = HostCollision::from_triangles(
            &quad([[-2.0, -2.0, 0.0], [2.0, -2.0, 0.0], [2.0, 2.0, 0.0], [-2.0, 2.0, 0.0]]),
            [0.0; 3],
        );
        for x in [-40.0, -10.0, 0.0, 0.01, 10.0, 40.0] {
            let land = trace(&scene, [x, x, 20.0], [x, x, -20.0]);
            assert!(land.fraction < 1.0 && land.normal[2] > 0.99, "{x}: {land:?}");
        }
    }
    #[test]
    fn walking_across_bridge_road_seams_does_not_snag() {
        // Road models meet with a slope change, a few millimetres of height
        // mismatch and a hairline overlap or gap.
        for (grade, mismatch, joint) in [
            (6.0f32, 0.005f32, 0.01f32),
            (6.0, -0.005, -0.01),
            (-6.0, 0.005, 0.0),
            (12.0, 0.01, 0.02),
            (0.0, 0.008, -0.005),
        ] {
            for crest in [false, true] {
            let z = 10.0 * grade.to_radians().tan();
            // A crest: the first piece climbs towards the joint.
            let rise = if crest { 10.0 * 6f32.to_radians().tan() } else { 0.0 };
            let mut triangles = Vec::new();
            triangles.extend(quad([
                [-10.0, -4.0, -rise],
                [0.0 + joint, -4.0, 0.0],
                [0.0 + joint, 4.0, 0.0],
                [-10.0, 4.0, -rise],
            ]));
            triangles.extend(quad([
                [0.0, -4.0, mismatch],
                [10.0, -4.0, mismatch + z],
                [10.0, 4.0, mismatch + z],
                [0.0, 4.0, mismatch],
            ]));
            let scene = HostCollision::from_triangles(&triangles, [0.0; 3]);
            let walked = walk(&scene, -5.0 * UNITS_PER_METER, 5.0 * UNITS_PER_METER, 30.0);
            assert!(
                walked >= 10.0 * UNITS_PER_METER - 2.0,
                "grade {grade} mismatch {mismatch} joint {joint} crest {crest}: stuck after {walked} units"
            );
            }
        }
    }
    fn trace(scene: &HostCollision, start: [f32; 3], end: [f32; 3]) -> Trace {
        scene.trace(GroundTraceInput {
            start,
            end,
            mins: [-15.0, -15.0, 0.0],
            maxs: [15.0, 15.0, 70.0],
            tracemask: 0x02810011,
        })
    }
    #[test]
    fn preserves_iw4_spawn_coordinates_when_gta_has_a_different_anchor() {
        let scene = HostCollision::from_triangles_at(
            &[HostTriangle {
                vertices: [
                    [2475.0, -1689.0, 12.3],
                    [2515.0, -1689.0, 12.3],
                    [2495.0, -1649.0, 12.3],
                ],
                surface: 0,
            }],
            [2495.0, -1669.0, 12.3],
            [954.0, 550.0, -243.0],
        );
        let result = trace(&scene, [954.0, 550.0, -223.0], [954.0, 550.0, -263.0]);
        assert!(
            result.fraction > 0.45 && result.fraction < 0.55,
            "{result:?}"
        );
        assert!(
            result.normal[2] > 0.99 && result.walkable != 0,
            "{result:?}"
        );
    }
    #[test]
    fn real_gta_floor_stops_iw4_capsule_with_walkable_up_normal() {
        let scene = HostCollision::from_triangles(
            &[HostTriangle {
                vertices: [[-20.0, -20.0, 0.0], [20.0, -20.0, 0.0], [0.0, 20.0, 0.0]],
                surface: 0,
            }],
            [0.0; 3],
        );
        let result = trace(&scene, [0.0, 0.0, 20.0], [0.0, 0.0, -20.0]);
        assert!(
            result.fraction > 0.45 && result.fraction < 0.55,
            "{result:?}"
        );
        assert!(
            result.normal[2] > 0.99 && result.walkable != 0,
            "{result:?}"
        );
    }
    #[test]
    fn real_gta_wall_blocks_capsule_before_its_center_crosses() {
        let scene = HostCollision::from_triangles(
            &[HostTriangle {
                vertices: [[2.54, -20.0, -20.0], [2.54, 20.0, -20.0], [2.54, 0.0, 20.0]],
                surface: 0,
            }],
            [0.0; 3],
        );
        let result = trace(&scene, [0.0, 0.0, 0.5], [200.0, 0.0, 0.5]);
        assert!(
            result.fraction > 0.40 && result.fraction < 0.45,
            "{result:?}"
        );
        assert!(
            result.normal[0] < -0.99 && result.walkable == 0,
            "{result:?}"
        );
    }
    #[test]
    fn unrelated_surfaces_do_not_block_and_reversed_winding_still_collides() {
        let scene = HostCollision::from_triangles(
            &[HostTriangle {
                vertices: [[-20.0, -20.0, 0.0], [0.0, 20.0, 0.0], [20.0, -20.0, 0.0]],
                surface: 0,
            }],
            [0.0; 3],
        );
        assert_eq!(
            trace(&scene, [2000.0, 2000.0, 20.0], [2000.0, 2000.0, -20.0]).fraction,
            1.0
        );
        assert!(trace(&scene, [0.0, 0.0, 20.0], [0.0, 0.0, -20.0]).normal[2] > 0.99);
    }
}
