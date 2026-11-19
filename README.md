# Angara FX

Angara FX is a 3D particle system for Godot 4 with a C++ runtime and CPU and GPU simulation. Build effects in the editor and control each instance from your game.

Effects are standard Godot text resources. An effect lives in a `.tres` file, a player scene references it from a `.tscn`, and multiple players can share the same effect with their own parameter values.

## Build and run

The current GDExtension manifest targets Windows x86-64 and the standard single-precision Godot 4.7 build. The example runs in Forward+ with Jolt Physics. GPU simulation requires a rendering device with compute support.

Install Git, Python 3, CMake 3.24 or later, and Visual Studio 2022 with the Desktop development with C++ workload and a Windows SDK. From the repository root, run:

```powershell
cmake -S native/niagara_bridge -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target niagara_bridge_native --parallel 6
```

The first configuration fetches the pinned godot-cpp revision. CMake places the DLL inside `addons/niagara_bridge/native/bin`, where Godot loads it. To use a local checkout of the same SDK, pass `-DFETCHCONTENT_SOURCE_DIR_GODOT_CPP=<path>` at configure time.

Open `project.godot` in Godot and press **F5** to run **Embers**. To edit the effect, open `examples/embers/embers.tres` in the **Angara FX** editor and press **Play** in its timeline. Change **User.EmissionRate** and save. The sprite material and shader are in the same example folder.

To use Angara FX in another Godot 4.7 project, copy the built `addons/niagara_bridge` directory and enable **Angara FX** in **Project Settings → Plugins**. Keep that directory name so saved resource paths continue to resolve. Authoring and playback run entirely in Godot.

## Control an instance

The example declares a float parameter called `User.EmissionRate`. Given an instance of the example scene, the following sets its emission rate:

```gdscript
$Effect.set_user_parameter_live("EmissionRate", 45.0)
```

## Verify the build

With Godot 4.7 on your PATH, run the following from the repository root. The smoke test opens the example in the authoring workspace, saves an edited copy in Godot's user data directory, reloads it, runs all three curl-field resolutions on CPU and GPU, and checks that the demo draws visible particles.

```powershell
godot --editor --import
godot --resolution 960x540 --fixed-fps 60 --script res://tests/core_smoke.gd
python tools/generate_curl_fields.py --check
```

The three tiled noise fields are original Angara FX assets. The generator samples the same periodic flow at three resolutions and writes readable `.tres` files. Imported effects that use Niagara's baked noise can therefore follow different trajectories.

## License

Angara FX is released under the [MIT License](LICENSE). The extension links [godot-cpp](https://github.com/godotengine/godot-cpp). Its license is included in the [third-party notices](addons/niagara_bridge/native/THIRD_PARTY_NOTICES.txt).
