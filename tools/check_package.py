"""Verify the Windows release ZIP, including path strings embedded in the DLL."""
import argparse
from pathlib import Path
import subprocess
from zipfile import ZipFile


def check(archive):
    repo = Path(__file__).resolve().parents[1]
    files = subprocess.check_output(["git", "-C", str(repo), "ls-files", "-z", "addons"])
    expected = set(files.decode().rstrip("\0").split("\0"))
    expected.update({"addons/angara_fx/LICENSE", "addons/angara_fx/native/bin/angara_fx_native.dll"})
    forbidden = ("niagara_bridge", "niagara-bridge", "niagara bridge", str(repo.resolve()),
                 ":\\users\\", ":/users/", "/home/", "/users/")
    patterns = [s.lower().encode(encoding) for s in forbidden for encoding in ("ascii", "utf-16-le")
                if encoding != "ascii" or s.isascii()]
    with ZipFile(archive) as package:
        names = [entry.filename for entry in package.infolist() if not entry.is_dir()]
        if set(names) != expected or len(names) != len(expected):
            raise SystemExit(f"Unexpected package contents: {sorted(set(names) ^ expected)}")
        for name in names:
            content = name.lower().encode() + package.read(name).lower()
            if any(pattern in content for pattern in patterns):
                raise SystemExit(f"Legacy namespace or local build path in {name}")
    print(f"Verified {len(names)} Angara FX files in {archive.name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    check(parser.parse_args().archive)
