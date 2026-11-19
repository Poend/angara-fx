@tool
extends NBNativeSourceClock
## NiagaraSystemSimulation.cpp1053–1080: fixed tick budget and ages are float32.
## The native cap drops excess whole ticks; it does not queue them for later.
const MAX_SUBSTEPS_CVAR: String = "fx.Niagara.SystemSimulation.MaxTickSubsteps"
