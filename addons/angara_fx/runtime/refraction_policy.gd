@tool
extends RefCounted
## Source evidence and supported policy validation execute natively.
const PRODUCER = "NiagaraBridgeSourceLibrary.DescribeRefractionPolicy"
const INTEGER_CVARS: Array[String] = ["r.RefractionQuality","r.DisableDistortion","r.Refraction.OffsetQuality","r.Refraction.Blur","r.Substrate","r.SeparateTranslucency","r.Translucency.StandardSeparated"]
static func inspect(policy: Variant) -> Dictionary:return NBNativeRenderPolicy.refraction(policy)
static func fingerprint(policy: Dictionary) -> String:return NBNativeRenderPolicy.fingerprint(policy)
static func for_effect(effect: Resource) -> Dictionary:return NBNativeRenderPolicy.for_effect(effect)
