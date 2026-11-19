@tool
extends RefCounted
## Scalar lane operations for optimized VectorVM. Values are uint32 register
## words so float comparisons, bit selection and integer arithmetic can alias.
const Math = preload("niagara_cpu_math.gd")
const RNG = preload("niagara_rng.gd")
const MASK: int = 0xffffffff

static func bits(value: float) -> int: return PackedFloat32Array([value]).to_byte_array().decode_u32(0)
static func number(word: int) -> float: return PackedInt32Array([signed(word)]).to_byte_array().decode_float(0)
static func signed(word: int) -> int: return (word & MASK) - 4294967296 if word & 0x80000000 else word & MASK
static func truth(value: bool) -> int: return MASK if value else 0
static func VectorCastIntToFloat(a: int) -> int: return a
static func VectorCastFloatToInt(a: int) -> int: return a
static func VectorZeroFloat() -> int: return 0
static func VectorSetZero() -> int: return 0
static func VectorIntSet1(a: int) -> int: return a & MASK
static func _constant(name: String) -> int:
	return bits({"One":1.0,"Epsilon":1e-8,"QuarterPi":Math.f32(PI) * 0.25,"NegativeOne":-1.0}[name])
static func VectorAdd(a: int, b: int) -> int: return bits(number(a) + number(b))
static func VectorSubtract(a: int, b: int) -> int: return bits(number(a) - number(b))
static func VectorMultiply(a: int, b: int) -> int: return bits(number(a) * number(b))
static func VectorMultiplyAdd(a: int, b: int, c: int) -> int: return VectorAdd(VectorMultiply(a,b),c)
static func VectorDivide(a: int, b: int) -> int:
	var denominator: float = number(b)
	if denominator == 0.0:
		if number(a) == 0.0: return bits(NAN)
		return bits(-INF if ((a ^ b) & 0x80000000) else INF)
	return bits(number(a) / denominator)
static func VectorLerp(a: int, b: int, c: int) -> int: return VectorMultiplyAdd(b,c,VectorMultiply(a,VectorSubtract(bits(1.0),c)))
static func VectorNegate(a: int) -> int: return a ^ 0x80000000
static func VectorAbs(a: int) -> int: return a & 0x7fffffff
static func VectorMin(a: int, b: int) -> int: return a if number(a) < number(b) else b
static func VectorMax(a: int, b: int) -> int: return a if number(a) > number(b) else b
static func VectorClamp(a: int, b: int, c: int) -> int: return VectorMin(VectorMax(a,b),c)
static func VectorSelect(a: int, b: int, c: int) -> int: return ((a & b) | ((~a) & c)) & MASK
static func VectorIntSelect(a: int, b: int, c: int) -> int: return VectorSelect(a,b,c)
static func VectorCompareLT(a: int, b: int) -> int: return truth(number(a) < number(b))
static func VectorCompareLE(a: int, b: int) -> int: return truth(number(a) <= number(b))
static func VectorCompareGT(a: int, b: int) -> int: return truth(number(a) > number(b))
static func VectorCompareGE(a: int, b: int) -> int: return truth(number(a) >= number(b))
static func VectorCompareEQ(a: int, b: int) -> int: return truth(number(a) == number(b))
static func VectorCompareNE(a: int, b: int) -> int: return truth(number(a) != number(b))
static func VectorIntCompareLT(a: int, b: int) -> int: return truth(signed(a) < signed(b))
static func VectorIntCompareLE(a: int, b: int) -> int: return truth(signed(a) <= signed(b))
static func VectorIntCompareGT(a: int, b: int) -> int: return truth(signed(a) > signed(b))
static func VectorIntCompareGE(a: int, b: int) -> int: return truth(signed(a) >= signed(b))
static func VectorIntCompareEQ(a: int, b: int) -> int: return truth(a == b)
static func VectorIntCompareNEQ(a: int, b: int) -> int: return truth(a != b)
static func VectorIntAdd(a: int, b: int) -> int: return (a + b) & MASK
static func VectorIntSubtract(a: int, b: int) -> int: return (a - b) & MASK
static func VectorIntMultiply(a: int, b: int) -> int: return RNG._mul32(a,b)
static func VVMIntDiv(a: int, b: int) -> int:
	if b == 0: return 0
	if a == 0x80000000 and b == MASK: return 0x7fffffff
	return int(float(signed(a)) / float(signed(b))) & MASK
static func VectorIntMin(a: int, b: int) -> int: return a if signed(a) < signed(b) else b
static func VectorIntMax(a: int, b: int) -> int: return a if signed(a) > signed(b) else b
static func VectorIntClamp(a: int, b: int, c: int) -> int: return VectorIntMin(VectorIntMax(a,b),c)
static func VectorIntAbs(a: int) -> int: return abs(signed(a)) & MASK
static func VectorIntNegate(a: int) -> int: return (-a) & MASK
static func VectorIntSign(a: int) -> int: return MASK if signed(a) < 0 else 1
static func VectorIntAnd(a: int, b: int) -> int: return a & b
static func VectorIntOr(a: int, b: int) -> int: return a | b
static func VectorIntXor(a: int, b: int) -> int: return a ^ b
static func VectorIntNot(a: int) -> int: return (~a) & MASK
static func VVMIntLShift(a: int, b: int) -> int: return (a << (b & 31)) & MASK
static func VVMIntRShift(a: int, b: int) -> int: return a >> (b & 31)
static func VectorIntToFloat(a: int) -> int: return bits(float(signed(a)))
static func VectorFloatToInt(a: int) -> int:
	var value: float = number(a)
	return 0x80000000 if not is_finite(value) or value >= 2147483648.0 or value < -2147483648.0 else int(value) & MASK
static func VVMf2i(a: int) -> int: return VectorFloatToInt(a)
static func VVMi2f(a: int) -> int: return VectorIntToFloat(a)
static func VectorReciprocalEstimate(a: int) -> int: return bits(Math.reciprocal(number(a)))
static func VectorReciprocalSqrt(a: int) -> int: return bits(Math.reciprocal_sqrt(number(a)))
static func VectorSqrt(a: int) -> int: return bits(sqrt(number(a)))
static func VectorExp(a: int) -> int: return bits(exp(number(a)))
static func VectorExp2(a: int) -> int: return bits(pow(2.0,number(a)))
static func VectorLog(a: int) -> int: return bits(log(number(a)))
static func VectorLog2(a: int) -> int: return bits(log(number(a))/log(2.0))
static func VectorSin(a: int) -> int:
	var value: float = number(a)
	return bits(Math.sin_turn(value) if absf(value) <= 6.28319 else sin(value))
static func VectorCos(a: int) -> int:
	var value: float = number(a)
	return bits(Math.cos_turn(value) if absf(value) <= 6.28319 else cos(value))
static func VectorTan(a: int) -> int: return bits(tan(number(a)))
static func VectorATan(a: int) -> int: return bits(atan(number(a)))
static func VectorATan2(a: int, b: int) -> int: return bits(atan2(number(a),number(b)))
static func VectorCeil(a: int) -> int: return bits(ceil(number(a)))
static func VectorFloor(a: int) -> int: return bits(floor(number(a)))
static func VectorTruncate(a: int) -> int: return bits(float(int(number(a)))) if is_finite(number(a)) else a
static func VectorFractional(a: int) -> int: return VectorSubtract(a,VectorTruncate(a))
static func VectorMod(a: int, b: int) -> int: return bits(fmod(number(a),number(b))) if number(b) != 0.0 else bits(NAN)
static func VectorPow(a: int, b: int) -> int: return bits(Math.positive_pow(number(a),number(b)))
static func VectorRound(a: int) -> int:
	var value: float = number(a)
	if not is_finite(value): return a
	var low: float = floor(value)
	return bits(low + (1.0 if value-low > 0.5 or (value-low == 0.5 and fmod(low,2.0) != 0.0) else 0.0))
static func VectorSign(a: int) -> int: return bits(1.0 if number(a) >= 0.0 else -1.0)
static func VectorStep(a: int) -> int: return bits(1.0 if number(a) >= 0.0 else 0.0)
