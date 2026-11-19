@tool
extends RefCounted
## Shared emitted-program reader for normal imports and controlled comparisons.
## Provenance and seed policy belong to each caller, never to this parser.
const Contracts=preload("rng_contract.gd")
const Program=preload("source_rng_program.gd")

## Native source artifact reader; HLSL contract extraction remains an explicit parser seam.

static func system_stages(manifest: Dictionary, hashes: Dictionary, directory: String, errors: Array) -> Dictionary:
	return NBNativeSourceReader.system_stages(manifest,hashes,directory,errors)

static func stages(emitter: Resource, manifest: Dictionary, hashes: Dictionary, directory: String, errors: Array, analysis_errors: Array) -> Dictionary:
	return NBNativeSourceReader.stages(emitter,manifest,hashes,directory,errors,analysis_errors,Contracts)

static func _system_spawn_contract(manifest: Dictionary,hashes: Dictionary,directory: String,errors: Array) -> Dictionary:
	return NBNativeSourceReader.system_spawn(manifest,hashes,directory,errors,Contracts)

static func vm_program(script: Dictionary, hashes: Dictionary, directory: String, errors: Array, analysis_errors: Array) -> Dictionary:
	return NBNativeSourceReader.vm_program(script,hashes,directory,errors,analysis_errors)

static func _relative_file(directory: String,relative: String,errors: Array) -> String:
	return NBNativeSourceReader.relative_file(directory,relative,errors)

static func _hash_matches(path: String,expected: String) -> bool:
	return NBNativeSourceReader.hash_matches(path,expected)
