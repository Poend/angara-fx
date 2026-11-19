@tool
extends NBNativeCPUMath
## Scalar emulation of the reviewed UE5.8 Win64/MSVC/AMD CPU math path.
## The reciprocal profile is text; playback needs no native library.
## GPU arithmetic has a separate implementation and must not use this profile.
const PROFILE = preload("res://addons/niagara_bridge/data/math/ue58_win64_amd.tres")
const PI_PARTS = [3.140625, 0.0009675025939941406, 1.5099067240953445e-7, 5.126688136514179e-12]
const TRIG_COEFFICIENTS = [2.605780537123792e-6, -0.00019809602235909551, 0.008333062753081322, -0.16666659712791443]
