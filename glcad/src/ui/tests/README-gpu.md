# GPU boolean preview checks

`gcapp_gpu_tests` runs the production preview shaders in an offscreen Vulkan
render pass and reads the resulting depth buffer. It requires a Vulkan device
and ICD; it is deliberately separate from the headless UI test suite.

```sh
cmake --build build/debug --target glcad gcapp_tests gcapp_gpu_tests
build/debug/glcad/src/ui/gcapp_gpu_tests
GLCAD_TEST_ONLY=modeller_gpu build/debug/glcad/src/ui/gcapp_tests
```

On macOS, use the Vulkan runtime environment in `build/debug/glcad.env`.
Set `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` when validation layers
are installed. GPU access must be allowed by the execution environment.

The GPU checks cover union, subtraction, intersection, coincident/touching
surfaces, blind/through cuts, total removal, disconnected bodies, holed curved
profiles exceeding the former fixed capacity, and cameras inside a solid.
They also check shallow side-face cuts using the app's orthographic and
perspective cameras, and grid stability when the cap is retriangulated.
Repeated circular extrusion reversals are compared pixel by pixel with a
separately constructed solid, including shallow edits and reuse of both frame
slots. These catch stray surfaces where the cutter shares the original walls.
The modeller checks cover concave/holed profiles in both extrusion directions,
preview cancellation, all three combine tools, undo, and final CPU volumes.

The optional real-window scenario is `GLCAD_SCENARIO=box-circle-extrude` with
`GLCAD_SCENARIO_QUIT=1`; set `GLCAD_SCENARIO_DISTANCE=-1` to exercise a cut.
Use a temporary `GLCAD_CONFIG_DIR` for an isolated run.
Set `GLCAD_SCENARIO_SIDE_FACE=1` and `GLCAD_SCENARIO_DISTANCE=-0.1218` to
exercise a shallow circular cut into a vertical face. The UI test suite checks
that this gesture commits a closed, manifold CPU solid.

The preview consumes tessellated operands and a CPU-built BVH. All boolean
classification happens in the fragment shader, which writes surface color
and depth. It produces no persistent mesh: the final operation still runs
through the CPU modeller. Precision is float32 and curved faces use their
render tessellation. An empty boolean is a valid preview; only unavailable
GPU resources cause fallback drawing.

For optional GPU timing, set `GLCAD_GPU_BENCHMARK=1` when running
`gcapp_gpu_tests`. It reports median Vulkan timestamp durations for a 512×512
holed-profile union, cut, and intersection after ten warmup frames. This
measures GPU rendering time, excluding CPU BVH construction and readback;
it does not measure power draw. It requires 64-bit timestamp support.

Operation amount fields accept clicks. Extrusion, round, chamfer, and shell
fields also accept unit-aware expressions. Editing an
extrusion value keeps the GPU preview active while the saved bodies stay
unchanged. Round, chamfer, shell, and pattern values use temporary display
bodies. OK or Enter applies the CPU operation; Escape discards the draft.
The `workflow_clickable` UI checks exercise actual field and button clicks,
preview isolation, cancellation, and one-time commit behavior. The modeller
cut test also checks GPU operands during typed editing.

The CPU circular side-cut regression can be timed independently:

```sh
GLCAD_TEST_ONLY=bool_circular_side_cut_preserves_its_original_edge_planes build/debug/glcad/src/geometry/gtopo_tests
```

It checks an 80-edge circular cut at both 32-bit and 40-bit lattice precision,
including closed topology, planarity, and final volume. The CPU optimization
stops splitting fragments proven outside a convex operand, and fragments
outside the other solid's bounds; nonconvex operands retain general splitting.

After a commit, Select can pick the resulting planar face or pocket floor.
Its Face offset starts at zero on the current face: positive values move
outward and negative values move inward. A consumed cut sketch does not hide
the floor from Select; explicitly arming Extrude still permits sketch reuse.
The `workflow_face_edit` checks cover consecutive signed offsets, exact final
volumes, pocket selection through the original sketch, and hover areas both
inside a sketch and on the surrounding face, including existing face holes.
Hover and selection fills have explicit scene ranges and a depth-tested,
read-only overlay pipeline. They are omitted during live boolean previews so
an original face cannot paint over a cut opening.

Consumed sketches hide automatically and appear beneath their solid in Objects.
Show/Hide overrides visibility; Auto restores automatic hiding. Unused regions
remain available, and editing a sketch temporarily reveals it. Visibility is
saved with the document and supports undo/redo. Hidden sketches cannot supply
hover regions, sweep paths, or picked sketch planes, but remain available for
CPU feature regeneration. The `workflow_consumed` checks cover these controls,
face picking after a feature, and circular sketches after switching or loading.

Numeric cards do not track the pointer or the moving extrusion arrow. Edit
fields sit beside the committed body or profile, and drawing fields occupy a
fixed side position below the view cube. Their widths stay fixed while values
change. The `workflow_static_input` checks cover pointer sweeps, typed previews,
and positive/negative extrusion drags without moving the input fields.
