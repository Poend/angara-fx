"""Update a Godot project's text references after installing Angara FX 0.2."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile


OLD = "res://addons/niagara_bridge/"
RESOURCE_PATH = re.compile(re.escape(OLD) + r"""([^"'\s\])]+)""")
META = re.compile(r"\bniagara_bridge_(advance_timings_usec|context|conversion|geometry_timings_usec|"
                  r"immutable_geometry|imported_texture|material_program|native_texture|pipeline|"
                  r"profile_renderer|refraction_timings_usec|renderer_timings_usec|simulation_timings_usec|"
                  r"system_material|system_program|unsaved_document|world_clock)\b")
TEXT = {".gd", ".tscn", ".tres", ".gdshader", ".gdshaderinc"}
SKIP = {"addons", "build", ".godot", ".git", ".import_backups"}


def project_files(root):
    documents, signed = [], set()
    for folder, dirs, names in os.walk(root):
        directory = Path(folder)
        dirs[:] = [d for d in dirs if d not in SKIP and not d.startswith(".")
                   and (directory / d).resolve() == directory / d]
        if (directory / ".gdignore").is_file():
            dirs.clear()
            continue
        for name in names:
            path = directory / name
            if path.is_symlink():
                continue
            if name == "bundle.json":
                try:
                    bundle = json.loads(path.read_bytes())
                except (ValueError, UnicodeError):
                    continue
                if isinstance(bundle, dict) and bundle.get("producer") == "NiagaraBridge.EffectBundle":
                    for entry in bundle.get("plans", []):
                        if isinstance(entry, dict) and isinstance(entry.get("plan"), str):
                            relative = entry["plan"].removeprefix("res://")
                            signed.add((root / relative).resolve())
            if path.suffix in TEXT or path == root / "project.godot":
                documents.append(path)
    return sorted(set(documents) - signed)


def rewrite(data, root, path):
    if b"niagara_bridge" not in data and b"NiagaraEffect3D" not in data:
        return data
    text = data.decode("utf-8")

    def resource(match):
        relative = match[1]
        if relative == "native/niagara_bridge.gdextension":
            relative = "native/angara_fx.gdextension"
        for addon in ("angara_fx", "niagara_importer"):
            dest = Path("addons", addon, relative)
            if (root / dest).is_file():
                return "res://" + dest.as_posix()
        return match[0]

    text = RESOURCE_PATH.sub(resource, text)
    text = META.sub(r"angara_fx_\1", text)
    if path.suffix == ".gd":
        text = re.sub(r"\bNiagaraEffect3D\b", "AngaraEffect3D", text)
    return text.encode("utf-8")


def migrate(root, apply):
    if not (root / "project.godot").is_file() or not (root / "addons/angara_fx/plugin.cfg").is_file():
        raise SystemExit("Choose a Godot project with Angara FX 0.2 already installed.")
    changes, reports = {}, []
    for path in project_files(root):
        original = path.read_bytes()
        updated = rewrite(original, root, path)
        if updated != original:
            changes[path] = (original, updated)
        if path.name == "import_report.tres":
            reports.append(path)

    # Re-key only dependency hashes that matched the exact pre-migration bytes.
    # Modified dependencies keep their original baseline for three-way reimport.
    for report in reports:
        raw = report.read_bytes()
        text = changes.get(report, (raw, raw))[1].decode("utf-8")
        def baselines(block):
            def dependency(entry):
                source = (report.parent / entry[1]).resolve()
                if source in changes:
                    before, after = changes[source]
                    if hashlib.sha256(before).hexdigest() == entry[2]:
                        return entry[0].replace(entry[2], hashlib.sha256(after).hexdigest())
                return entry[0]
            return re.sub(r'"([^"\r\n]+)": "([0-9a-f]{64})"', dependency, block[0])
        updated = re.sub(r'"dependency_baselines": \{[^}]*\}', baselines, text).encode("utf-8")
        if updated != raw:
            changes[report] = (raw, updated)

    # Commit new baselines first so an interrupted run can finish the remaining files on retry.
    for path in sorted(changes, key=lambda p: p.name != "import_report.tres"):
        old, new = changes[path]
        print(path.relative_to(root))
        if apply:
            if path.read_bytes() != old:
                raise SystemExit(f"File changed during migration: {path}")
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temp:
                temp.write(new)
            name = Path(temp.name)
            try:
                name.chmod(stat.S_IMODE(path.stat().st_mode))
                os.replace(name, path)
            finally:
                name.unlink(missing_ok=True)
    print(f"{len(changes)} file(s) " + ("updated" if apply else "would change"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--apply", action="store_true", help="Write changes. The default only lists them.")
    args = parser.parse_args()
    migrate(args.project.resolve(), args.apply)
