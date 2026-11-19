# Angara FX roadmap

Angara FX is a standalone particle system for Godot. The next milestone is **0.2.0**, focused on the workflow from a blank effect to a reusable scene. Later milestones are ordered by dependency. Their scope can change as we test more effects.

## 0.1.0 · Baseline

The Godot editor can create emitters, bind module inputs to user parameters, preview an effect, and save it as a text resource. A C++ GDExtension runs CPU simulation and GPU compute for supported modules. Two players can share a resource and override its parameters independently.

The tested configuration is Godot 4.7.2 on Windows x86-64, with Forward+ and Jolt. Embers is the reproducible example. The smoke test saves and reopens an edited effect, compares CPU and GPU positions across three curl fields, and checks the rendered image. [Release history](CHANGELOG.md) · [Smoke test](tests/core_smoke.gd)

## 0.2.0 · Build effects in Godot

- Publish a module reference showing the stage, supported inputs, and CPU/GPU coverage for each authorable module. Surface unsupported combinations in the editor with the emitter, module, and input that need attention.
- Build a one-shot burst and a continuous multi-emitter effect in Godot using the existing spawn, force, curve, and parameter modules. Fill gaps in shared modules so both examples use the same editor and runtime paths.
- Test the full editing loop, including undo, saving, reopening, and applying a saved edit while the game is running. Capture frame times on these scenes to establish a performance baseline.

**Milestone check.** Starting from a clean project, create both effects in the editor, save them as `.tres`, place two instances in a level, and change one instance from GDScript. Reopen the project and verify the saved bindings, the other instance’s defaults, and the result on each advertised backend.

## 0.3.0 · Renderers, events, and collision

- Expand renderer coverage beyond the Embers sprite test. Use reference scenes for mesh orientation and materials, ribbon continuity as the camera moves, and sorting where transparent particles overlap.
- Exercise event-driven emission and Jolt contact responses. Specify which event and collision modes each backend supports and report unsupported requests in the editor.

**Milestone check.** Replay fixed-seed scenes for a mesh emitter, a moving ribbon, and an impact spawning a second emitter. Compare particle counts, collision positions, and rendered frames with checked-in references and documented tolerances. Flag any renderer or backend still missing a reference case.

## 0.4.0 · Frame time and distribution

- Profile CPU simulation, GPU dispatch, transfers, and rendering separately. Start with expensive frames in the multi-emitter and transparency scenes. Keep reference images and particle counts alongside timings to catch visible regressions.
- Package the Windows x86-64 add-on and test installation in a fresh project. Add other platforms to the supported list as equivalent builds pass the same installation and playback checks.

**Milestone check.** Publish the hardware, scene, resolution, and frame-time distribution for uncapped runs. The target at 1440 × 900 is a maximum frame time of 16.67 ms after warm-up on the reference scenes, with 10 ms as the stretch target. Record the median, 99th percentile, and worst frame from real-time playback.

## 1.0 · Compatibility commitment

Stabilize the player API, parameter names and types, and the saved effect format. Keep effects authored on earlier versions as upgrade fixtures. A release candidate should open those fixtures, explain any migration, run the published renderer and performance checks, and load from a packaged build in a clean Godot project.

## Optional Niagara integration

The Unreal exporter and Godot importer have their own release cycle. Their compatibility contract is the core effect schema. Validate each supported importer/core pairing by importing, reimporting, and playing a representative effect, then comparing its appearance against the Unreal capture. Niagara Fluids and live gas simulation are deferred to a separate project.
