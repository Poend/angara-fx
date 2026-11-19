# Versioning and releases

The Godot add-on and its native runtime ship as one product. The `version` field in [plugin.cfg](addons/angara_fx/plugin.cfg) is the source of truth. CMake reads it during configuration. Release tags add a leading `v`, for example `v0.1.0`.

## Choosing a version

Angara FX uses numeric `MAJOR.MINOR.PATCH` versions. During initial development, a patch such as `0.1.1` preserves existing player calls and saved effects. A minor version such as `0.2.0` may add features or introduce a breaking change. List breaking changes in the changelog with the affected API or resource and the upgrade steps. From 1.0 onward, compatible features increment MINOR, compatible fixes increment PATCH, and incompatible public API changes increment MAJOR. [Semantic Versioning](https://semver.org/spec/v2.0.0.html)

The Godot compatibility minimum is declared in the [GDExtension manifest](addons/angara_fx/native/angara_fx.gdextension). The `schema_version` field in the [effect resource](addons/angara_fx/document/nb_effect.gd) tracks the on-disk format. Bump it when loading a saved effect needs a new interpretation, and add a fixture that exercises the old format. A core release can keep the same schema for many versions. The optional Niagara importer should declare which core versions and schemas it supports.

## Cutting a release

1. Choose the version for the changes being shipped. Update `plugin.cfg` and move the relevant `Unreleased` notes under a heading such as `## 0.2.0` in the [changelog](CHANGELOG.md). Record the release date. Leave later plans in the [roadmap](ROADMAP.md).
2. Check the candidate tag from the repository root. The command verifies the format and agreement with the newest changelog entry.

   ```powershell
   cmake "-DRELEASE_TAG=v0.2.0" -P native/angara_fx/version.cmake
   ```

3. Build the native extension and run the [verification commands](README.md#verify-the-build). Create the ZIP using the CMake `PACKAGE` target. Run `python tools/check_package.py build/angara-fx-0.2.0-windows-x86_64.zip` with the release's filename. Install that archive in a fresh project and repeat playback.
4. After the release changes are merged, check out the exact commit tested on `main`. Create an annotated tag at that commit and push only that tag. Substitute the chosen version in both commands.

   ```powershell
   git tag -a v0.2.0 HEAD -m "Angara FX 0.2.0"
   git push origin v0.2.0
   ```

Published tags are permanent. If a release needs a correction, publish a new version. A GitHub Release can attach the verified binary and reuse the matching changelog entry.
