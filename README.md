# GLCAD

GLCAD is source-available under the [GLCAD Noncommercial Source License](LICENSE).
Noncommercial use is free. Commercial CAD use requires a paid license through
the official App Store offering or a written agreement with Geramy Loveless.
Commercializing the software itself requires a separate written agreement;
see [commercial-use terms](COMMERCIAL-LICENSE.md).

One application for 2D drafting and 3D solid modeling, on the same document,
with the same geometry. Built for iPad and Apple Pencil, and runs on Mac,
Windows and Linux from source.

**Early access.** GLCAD is in active development and new features land
regularly. Expect rough edges, and please report what you find.

## What it does

- **2D drafting** — AutoCAD-style command line and aliases, object snaps,
  layers, blocks, hatches, dimensions, text, layouts and plot styles.
- **3D modeling** — tap a closed area of a sketch to make it a solid; push/pull,
  extrude, revolve, fillet, shell, cut and join; feature history with undo.
- **Architecture** — `WALLS` turns plan lines into joined 3D walls; `LIFT`
  raises a whole plan.
- **Making parts** — export STL and 3MF in millimetres for 3D printing, and
  STEP for machining and other CAD tools.
- **Interop** — DWG and DXF read/write, STEP and Parasolid X_T, DGN import,
  PDF and DWFx sheet-set publishing.
- **Pencil first** — on iPad the Pencil draws and fingers navigate.

## Getting the app

The App Store edition for iPad and Apple Silicon Macs includes a license for
commercial work. Building from source is free for noncommercial use.

## Building

```sh
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug -L fast
```

Presets: `debug`, `release`, `relwithdebinfo`, `asan`, `novulkan`.
Xcode projects: `scripts/xcode-macos.sh` and `scripts/xcode-ios.sh`
(`--simulator` for the iPad simulator).

Vulkan-Headers and volk are fetched and pinned by CMake. The ICD (MoltenVK on
Apple) is located, not fetched — `brew install molten-vk vulkan-loader
vulkan-validationlayers glslang`, or install the LunarG SDK and set `VULKAN_SDK`.

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; it
explains the contributor terms. Report security issues as described in
[SECURITY.md](SECURITY.md).
