@tool
extends "native_vm_scalar.gd"
## Generated opcode composition; edit scalar helpers or generator.
## Native VectorVMRuntime.cpp SHA256: 2e48b97a5c2a380f661cca0074c003ee535af3758f97f856864c8448810a2ca6
const ARITY: Dictionary = {
	"add": 2,
	"sub": 2,
	"mul": 2,
	"div": 2,
	"mad": 3,
	"lerp": 3,
	"rcp": 1,
	"rsq": 1,
	"sqrt": 1,
	"neg": 1,
	"abs": 1,
	"exp": 1,
	"exp2": 1,
	"log": 1,
	"log2": 1,
	"sin": 1,
	"cos": 1,
	"tan": 1,
	"acos": 1,
	"asin": 1,
	"atan": 1,
	"atan2": 2,
	"ceil": 1,
	"floor": 1,
	"fmod": 2,
	"frac": 1,
	"trunc": 1,
	"clamp": 3,
	"min": 2,
	"max": 2,
	"pow": 2,
	"round": 1,
	"sign": 1,
	"step": 2,
	"noise": 1,
	"cmplt": 2,
	"cmple": 2,
	"cmpgt": 2,
	"cmpge": 2,
	"cmpeq": 2,
	"cmpneq": 2,
	"select": 3,
	"addi": 2,
	"subi": 2,
	"muli": 2,
	"divi": 2,
	"clampi": 3,
	"mini": 2,
	"maxi": 2,
	"absi": 1,
	"negi": 1,
	"signi": 1,
	"cmplti": 2,
	"cmplei": 2,
	"cmpgti": 2,
	"cmpgei": 2,
	"cmpeqi": 2,
	"cmpneqi": 2,
	"bit_and": 2,
	"bit_or": 2,
	"bit_xor": 2,
	"bit_not": 1,
	"bit_lshift": 2,
	"bit_rshift": 2,
	"logic_and": 2,
	"logic_or": 2,
	"logic_xor": 2,
	"logic_not": 1,
	"f2i": 1,
	"i2f": 1,
	"f2b": 1,
	"b2f": 1,
	"i2b": 1,
	"b2i": 1,
	"noise2D": 2,
	"noise3D": 3,
	"cmplt_select": 4,
	"cmple_select": 4,
	"cmpeq_select": 4,
	"cmplti_select": 4,
	"cmplei_select": 4,
	"cmpeqi_select": 4,
	"cmplt_logic_and": 3,
	"cmple_logic_and": 3,
	"cmpgt_logic_and": 3,
	"cmpge_logic_and": 3,
	"cmpeq_logic_and": 3,
	"cmpne_logic_and": 3,
	"cmplti_logic_and": 3,
	"cmplei_logic_and": 3,
	"cmpgti_logic_and": 3,
	"cmpgei_logic_and": 3,
	"cmpeqi_logic_and": 3,
	"cmpnei_logic_and": 3,
	"cmplt_logic_or": 3,
	"cmple_logic_or": 3,
	"cmpgt_logic_or": 3,
	"cmpge_logic_or": 3,
	"cmpeq_logic_or": 3,
	"cmpne_logic_or": 3,
	"cmplti_logic_or": 3,
	"cmplei_logic_or": 3,
	"cmpgti_logic_or": 3,
	"cmpgei_logic_or": 3,
	"cmpeqi_logic_or": 3,
	"cmpnei_logic_or": 3,
	"mad_add": 4,
	"mad_sub0": 4,
	"mad_sub1": 4,
	"mad_mul": 4,
	"mad_sqrt": 3,
	"mad_mad0": 5,
	"mad_mad1": 5,
	"mul_mad0": 4,
	"mul_mad1": 4,
	"mul_add": 3,
	"mul_sub0": 3,
	"mul_sub1": 3,
	"mul_mul": 3,
	"mul_max": 3,
	"mul_2x": 2,
	"add_mad1": 4,
	"add_add": 3,
	"sub_cmplt1": 3,
	"sub_neg": 2,
	"sub_mul": 3,
	"div_mad0": 4,
	"div_f2i": 2,
	"div_mul": 3,
	"muli_addi": 3,
	"addi_bit_rshift": 3,
	"addi_muli": 3,
	"b2i_2x": 1,
	"i2f_div0": 2,
	"i2f_div1": 2,
	"i2f_mul": 2,
	"i2f_mad0": 3,
	"i2f_mad1": 3,
	"f2i_select1": 3,
	"f2i_maxi": 2,
	"f2i_addi": 2,
	"fmod_add": 3,
	"bit_and_i2f": 2,
	"bit_rshift_bit_and": 3,
	"neg_cmplt": 2,
	"bit_or_muli": 3,
	"bit_lshift_bit_or": 3,
	"max_f2i": 2,
	"select_mul": 4,
	"select_add": 4,
}

static func scalar(op: String, values: Array) -> int:
	var a: int = int(values[0]) if values.size() > 0 else 0
	var b: int = int(values[1]) if values.size() > 1 else 0
	var c: int = int(values[2]) if values.size() > 2 else 0
	var d: int = int(values[3]) if values.size() > 3 else 0
	var e: int = int(values[4]) if values.size() > 4 else 0
	match op:
		"add": return VectorAdd(a, b)
		"sub": return VectorSubtract(a, b)
		"mul": return VectorMultiply(a, b)
		"div": return VectorSelect(VectorCompareGT(VectorAbs(b), _constant("Epsilon")), VectorDivide(a, b), VectorZeroFloat())
		"mad": return VectorMultiplyAdd(a, b, c)
		"lerp": return VectorLerp(a, b, c)
		"rcp": return VectorSelect(VectorCompareGT(VectorAbs(a) , _constant("Epsilon")), VectorReciprocalEstimate(a), VectorZeroFloat())
		"rsq": return VectorSelect(VectorCompareGT(a, _constant("Epsilon")), VectorReciprocalSqrt(a), VectorZeroFloat())
		"sqrt": return VectorSelect(VectorCompareGT(a, _constant("Epsilon")), VectorSqrt(a), VectorZeroFloat())
		"neg": return VectorNegate(a)
		"abs": return VectorAbs(a)
		"exp": return VectorExp(a)
		"exp2": return VectorExp2(a)
		"log": return VectorSelect(VectorCompareGT(a, VectorZeroFloat()), VectorLog(a), VectorZeroFloat())
		"log2": return VectorLog2(a)
		"sin": return VectorSin(a)
		"cos": return VectorCos(a)
		"tan": return VectorTan(a)
		"acos": return VectorATan2(scalar("sqrt", [VectorMultiply(VectorSubtract(_constant("One"), a), VectorAdd(_constant("One"), a))]), a)
		"asin": return VectorSubtract(_constant("QuarterPi"), scalar("acos", [a]))
		"atan": return VectorATan(a)
		"atan2": return VectorATan2(a, b)
		"ceil": return VectorCeil(a)
		"floor": return VectorFloor(a)
		"fmod": return VectorMod(a, b)
		"frac": return VectorFractional(a)
		"trunc": return VectorTruncate(a)
		"clamp": return VectorClamp(a, b, c)
		"min": return VectorMin(a, b)
		"max": return VectorMax(a, b)
		"pow": return VectorSelect(VectorCompareGT(a, _constant("Epsilon")), VectorPow(a, b), VectorZeroFloat())
		"round": return VectorRound(a)
		"sign": return VectorSign(a)
		"step": return VectorStep(VectorSubtract(b, a))
		"noise": return 0
		"cmplt": return VectorCompareLT(a, b)
		"cmple": return VectorCompareLE(a, b)
		"cmpgt": return VectorCompareGT(a, b)
		"cmpge": return VectorCompareGE(a, b)
		"cmpeq": return VectorCompareEQ(a, b)
		"cmpneq": return VectorCompareNE(a, b)
		"select": return VectorSelect(a, b, c)
		"addi": return VectorIntAdd(a, b)
		"subi": return VectorIntSubtract(a, b)
		"muli": return VectorIntMultiply(a, b)
		"divi": return VVMIntDiv(a, b)
		"clampi": return VectorIntClamp(a, b, c)
		"mini": return VectorIntMin(a, b)
		"maxi": return VectorIntMax(a, b)
		"absi": return VectorIntAbs(a)
		"negi": return VectorIntNegate(a)
		"signi": return VectorIntSign(a)
		"cmplti": return VectorIntCompareLT(a, b)
		"cmplei": return VectorIntCompareLE(a, b)
		"cmpgti": return VectorIntCompareGT(a, b)
		"cmpgei": return VectorIntCompareGE(a, b)
		"cmpeqi": return VectorIntCompareEQ(a, b)
		"cmpneqi": return VectorIntCompareNEQ(a, b)
		"bit_and": return VectorIntAnd(a, b)
		"bit_or": return VectorIntOr(a, b)
		"bit_xor": return VectorIntXor(a, b)
		"bit_not": return VectorIntNot(a)
		"bit_lshift": return VVMIntLShift(a, b)
		"bit_rshift": return VVMIntRShift(a, b)
		"logic_and": return VectorIntAnd(a, b)
		"logic_or": return VectorIntOr(a, b)
		"logic_xor": return VectorIntXor(a, b)
		"logic_not": return VectorIntNot(a)
		"f2i": return VVMf2i(a)
		"i2f": return VVMi2f(a)
		"f2b": return VectorCompareGT(a, VectorZeroFloat())
		"b2f": return VectorSelect(a, _constant("One"), VectorZeroFloat())
		"i2b": return VectorIntCompareGT(a, VectorSetZero())
		"b2i": return VectorIntSelect(a, VectorIntSet1(1), VectorSetZero())
		"noise2D": return 0
		"noise3D": return 0
		"cmplt_select": return VectorSelect(VectorCompareLT(a, b), c, d)
		"cmple_select": return VectorSelect(VectorCompareLE(a, b), c, d)
		"cmpeq_select": return VectorSelect(VectorCompareEQ(a, b), c, d)
		"cmplti_select": return VectorSelect(VectorCastIntToFloat(VectorIntCompareLT(a, b)), c, d)
		"cmplei_select": return VectorSelect(VectorCastIntToFloat(VectorIntCompareLE(a, b)), c, d)
		"cmpeqi_select": return VectorSelect(VectorCastIntToFloat(VectorIntCompareEQ(a, b)), c, d)
		"cmplt_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareLT(a, b)), c))
		"cmple_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareLE(a, b)), c))
		"cmpgt_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareGT(a, b)), c))
		"cmpge_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareGE(a, b)), c))
		"cmpeq_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareEQ(a, b)), c))
		"cmpne_logic_and": return VectorCastIntToFloat(VectorIntAnd(VectorCastFloatToInt(VectorCompareNE(a, b)), c))
		"cmplti_logic_and": return VectorIntAnd(VectorIntCompareLT(a, b), c)
		"cmplei_logic_and": return VectorIntAnd(VectorIntCompareLE(a, b), c)
		"cmpgti_logic_and": return VectorIntAnd(VectorIntCompareGT(a, b), c)
		"cmpgei_logic_and": return VectorIntAnd(VectorIntCompareGE(a, b), c)
		"cmpeqi_logic_and": return VectorIntAnd(VectorIntCompareEQ(a, b), c)
		"cmpnei_logic_and": return VectorIntAnd(VectorIntCompareNEQ(a, b), c)
		"cmplt_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareLT(a, b)), c))
		"cmple_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareLE(a, b)), c))
		"cmpgt_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareGT(a, b)), c))
		"cmpge_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareGE(a, b)), c))
		"cmpeq_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareEQ(a, b)), c))
		"cmpne_logic_or": return VectorCastIntToFloat(VectorIntOr(VectorCastFloatToInt(VectorCompareNE(a, b)), c))
		"cmplti_logic_or": return VectorIntOr(VectorIntCompareLT(a, b), c)
		"cmplei_logic_or": return VectorIntOr(VectorIntCompareLE(a, b), c)
		"cmpgti_logic_or": return VectorIntOr(VectorIntCompareGT(a, b), c)
		"cmpgei_logic_or": return VectorIntOr(VectorIntCompareGE(a, b), c)
		"cmpeqi_logic_or": return VectorIntOr(VectorIntCompareEQ(a, b), c)
		"cmpnei_logic_or": return VectorIntOr(VectorIntCompareNEQ(a, b), c)
		"mad_add": return VectorAdd(VectorMultiplyAdd(a, b, c), d)
		"mad_sub0": return VectorSubtract(VectorMultiplyAdd(a, b, c), d)
		"mad_sub1": return VectorSubtract(d, VectorMultiplyAdd(a, b, c))
		"mad_mul": return VectorMultiply(VectorMultiplyAdd(a, b, c), d)
		"mad_sqrt": return VectorSqrt(VectorMultiplyAdd(a, b, c))
		"mad_mad0": return VectorMultiplyAdd(d, e, VectorMultiplyAdd(a, b, c))
		"mad_mad1": return VectorMultiplyAdd(VectorMultiplyAdd(a, b, c), d, e)
		"mul_mad0": return VectorMultiplyAdd(VectorMultiply(a, b), c, d)
		"mul_mad1": return VectorMultiplyAdd(c, d, VectorMultiply(a, b))
		"mul_add": return VectorAdd(VectorMultiply(a, b), c)
		"mul_sub0": return VectorSubtract(VectorMultiply(a, b), c)
		"mul_sub1": return VectorSubtract(c, VectorMultiply(a, b))
		"mul_mul": return VectorMultiply(VectorMultiply(a, b), c)
		"mul_max": return VectorMax(VectorMultiply(a, b), c)
		"mul_2x": return VectorMultiply(a, b)
		"add_mad1": return VectorMultiplyAdd(c, d, VectorAdd(a, b))
		"add_add": return VectorAdd(VectorAdd(a, b), c)
		"sub_cmplt1": return VectorCompareLT(c, VectorSubtract(a, b))
		"sub_neg": return VectorNegate(VectorSubtract(a, b))
		"sub_mul": return VectorMultiply(VectorSubtract(a, b), c)
		"div_mad0": return VectorMultiplyAdd(scalar("div", [a, b]), c, d)
		"div_f2i": return VectorFloatToInt(scalar("div", [VectorCastIntToFloat(a), VectorCastIntToFloat(b)]))
		"div_mul": return VectorMultiply(scalar("div", [a, b]), c)
		"muli_addi": return VectorIntAdd(VectorIntMultiply(a, b), c)
		"addi_bit_rshift": return VVMIntRShift(VectorIntAdd(a, b), c)
		"addi_muli": return VectorIntMultiply(VectorIntAdd(a, b), c)
		"b2i_2x": return VectorIntSelect(a, VectorIntSet1(1), VectorSetZero())
		"i2f_div0": return scalar("div", [VectorIntToFloat(VectorCastFloatToInt(a)), b])
		"i2f_div1": return scalar("div", [b, VectorIntToFloat(VectorCastFloatToInt(a))])
		"i2f_mul": return VectorMultiply(VectorIntToFloat(VectorCastFloatToInt(a)), b)
		"i2f_mad0": return VectorMultiplyAdd(VectorIntToFloat(VectorCastFloatToInt(a)), b, c)
		"i2f_mad1": return VectorMultiplyAdd(a, b, VectorIntToFloat(VectorCastFloatToInt(c)))
		"f2i_select1": return VectorIntSelect(a, VectorFloatToInt(VectorCastIntToFloat(b)), c)
		"f2i_maxi": return VectorIntMax(VectorFloatToInt(VectorCastIntToFloat(a)), b)
		"f2i_addi": return VectorIntAdd(VectorFloatToInt(VectorCastIntToFloat(a)), b)
		"fmod_add": return VectorAdd(VectorMod(a, b), c)
		"bit_and_i2f": return VectorIntToFloat(VectorIntAnd(VectorCastFloatToInt(a), VectorCastFloatToInt(b)))
		"bit_rshift_bit_and": return VectorIntAnd(VVMIntRShift(a, b), c)
		"neg_cmplt": return VectorCompareLT(VectorNegate(a), b)
		"bit_or_muli": return VectorIntMultiply(VectorIntOr(a, b), c)
		"bit_lshift_bit_or": return VectorIntOr(VVMIntLShift(a, b), c)
		"max_f2i": return VectorFloatToInt(VectorMax(VectorCastIntToFloat(a), VectorCastIntToFloat(b)))
		"select_mul": return VectorMultiply(VectorSelect(a, b, c), d)
		"select_add": return VectorAdd(VectorSelect(a, b, c), d)
	assert(false, "Unsupported VM opcode: " + op)
	return 0
