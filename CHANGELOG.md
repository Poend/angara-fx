# Changelog

This file records user-visible changes to the Angara FX core. Planned work lives in the [roadmap](ROADMAP.md).

## Unreleased

- The native build reads the version from the Godot plugin manifest. Maintainers can check a proposed release tag and the changelog before publishing. [Release guide](RELEASING.md)

## 0.1.0

November 19, 2026 · First versioned baseline.

- Create an effect in the Godot editor from emitter stacks, bind module inputs to typed user parameters, and preview the result on the timeline.
- Save an effect as a text `.tres` resource and generate a `.tscn` player scene. Game code can override a declared parameter on one instance and restore its saved default.
- Run supported particle modules on CPU or GPU with the C++ GDExtension. The Windows x86-64 example uses Forward+ and Jolt in Godot 4.7.2.
- Open the included Embers example or run the smoke test for text save/reload, CPU/GPU curl fields, and visible output. [Source at v0.1.0](https://github.com/Poend/angara-fx/tree/v0.1.0)
