"""Regenerate Angara's periodic curl fields from analytic plane waves (Python 3)."""
import argparse
import math
from pathlib import Path

OUTPUT = Path(__file__).resolve().parents[1] / "addons/angara_fx/data/vector_fields"
# Each amplitude is perpendicular to its wave vector, so every term is divergence-free.
# Integer frequencies and phases close the field continuously at every tile boundary.
MODES = (
    ((1, 1, 0), (5, -5, 3), 7),
    ((0, 1, 2), (3, -4, 2), 39),
    ((2, 0, 1), (-2, 4, 4), 81),
    ((1, -2, 1), (3, 2, 1), 21),
    ((3, 1, -1), (1, -2, 1), 99),
    ((-1, 2, 3), (2, 1, 0), 61),
)
PERIOD = 128
WAVE = tuple(round(1024 * math.sin(math.tau * i / PERIOD)) for i in range(PERIOD))


def resource(size):
    header = f'''[gd_resource type="Resource" format=3]

[ext_resource type="Script" path="res://addons/angara_fx/document/nb_vector_field.gd" id="field"]

[resource]
script = ExtResource("field")
size = Vector3i({size}, {size}, {size})
bounds_min = PackedFloat64Array(-500, -500, 0)
bounds_max = PackedFloat64Array(500, 500, 1000)
vectors_xyz = PackedFloat32Array('''
    # Sample voxel centres, matching the CPU trilinear sampler and GPU texture lookup.
    centres = [(2 * i + 1) * (PERIOD // (2 * size)) for i in range(size)]
    rows = []
    for z in centres:
        for y in centres:
            row = []
            for x in centres:
                vx = vy = vz = 0
                for (kx, ky, kz), (ax, ay, az), phase in MODES:
                    wave = WAVE[(kx*x + ky*y + kz*z + phase) % PERIOD]
                    vx += ax * wave
                    vy += ay * wave
                    vz += az * wave
                row.extend(f"{v / 16384:.6f}" for v in (vx, vy, vz))
            rows.append(", ".join(row))
    return header + ",\n".join(rows) + ")\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the checked-in text resources")
    args = parser.parse_args()
    if not args.check:
        OUTPUT.mkdir(parents=True, exist_ok=True)
    for size in (16, 32, 64):
        path = OUTPUT / f"TilingCurl{size}.tres"
        expected = resource(size)
        if args.check:
            if path.read_text(encoding="utf-8") != expected:
                raise SystemExit(f"Field differs from generator: {path}")
        else:
            path.write_bytes(expected.encode("utf-8"))
        print(f"{size}^3: {path.name}")


if __name__ == "__main__":
    main()
