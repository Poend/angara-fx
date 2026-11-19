#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <xmmintrin.h>

// Word-based register arithmetic: no Variant conversion or scripting per lane.
namespace nbvm {
inline uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline float number(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline int32_t signed_word(uint32_t u) {
    int32_t i;
    std::memcpy(&i, &u, 4);
    return i;
}
inline uint32_t truth(bool b) {
    return b ? UINT32_MAX : 0;
}
inline uint32_t VectorCastIntToFloat(uint32_t a) {
    return a;
}
inline uint32_t VectorCastFloatToInt(uint32_t a) {
    return a;
}
inline uint32_t VectorZeroFloat() {
    return 0;
}
inline uint32_t VectorSetZero() {
    return 0;
}
inline uint32_t VectorIntSet1(uint32_t a) {
    return a;
}
inline uint32_t _constant(std::string_view name) {
    if (name == "One")
        return bits(1.f);
    if (name == "Epsilon")
        return bits(1e-8f);
    if (name == "QuarterPi")
        return bits(3.141592741012573242f * .25f);
    return bits(-1.f);
}
inline uint32_t VectorAdd(uint32_t a, uint32_t b) {
    return bits(number(a) + number(b));
}
inline uint32_t VectorSubtract(uint32_t a, uint32_t b) {
    return bits(number(a) - number(b));
}
inline uint32_t VectorMultiply(uint32_t a, uint32_t b) {
    return bits(number(a) * number(b));
}
inline uint32_t VectorMultiplyAdd(uint32_t a, uint32_t b, uint32_t c) {
    return VectorAdd(VectorMultiply(a, b), c);
}
inline uint32_t VectorDivide(uint32_t a, uint32_t b) {
    return bits(number(a) / number(b));
}
inline uint32_t VectorLerp(uint32_t a, uint32_t b, uint32_t c) {
    return VectorMultiplyAdd(b, c, VectorMultiply(a, VectorSubtract(bits(1.f), c)));
}
inline uint32_t VectorNegate(uint32_t a) {
    return a ^ 0x80000000u;
}
inline uint32_t VectorAbs(uint32_t a) {
    return a & 0x7fffffffu;
}
inline uint32_t VectorMin(uint32_t a, uint32_t b) {
    return number(a) < number(b) ? a : b;
}
inline uint32_t VectorMax(uint32_t a, uint32_t b) {
    return number(a) > number(b) ? a : b;
}
inline uint32_t VectorClamp(uint32_t a, uint32_t b, uint32_t c) {
    return VectorMin(VectorMax(a, b), c);
}
inline uint32_t VectorSelect(uint32_t a, uint32_t b, uint32_t c) {
    return (a & b) | (~a & c);
}
inline uint32_t VectorIntSelect(uint32_t a, uint32_t b, uint32_t c) {
    return VectorSelect(a, b, c);
}
#define NBVM_COMPARE(name, sign, op)                                                                         \
    inline uint32_t name(uint32_t a, uint32_t b) {                                                           \
        return truth(sign(a) op sign(b));                                                                    \
    }
NBVM_COMPARE(VectorCompareLT, number, <)
NBVM_COMPARE(VectorCompareLE, number, <=)
NBVM_COMPARE(VectorCompareGT, number, >) NBVM_COMPARE(VectorCompareGE, number, >=)
    NBVM_COMPARE(VectorCompareEQ, number, ==) NBVM_COMPARE(VectorCompareNE, number, !=)
        NBVM_COMPARE(VectorIntCompareLT, signed_word, <) NBVM_COMPARE(VectorIntCompareLE, signed_word, <=)
            NBVM_COMPARE(VectorIntCompareGT, signed_word, >) NBVM_COMPARE(VectorIntCompareGE, signed_word, >=)
                NBVM_COMPARE(VectorIntCompareEQ, signed_word, ==)
                    NBVM_COMPARE(VectorIntCompareNEQ, signed_word, !=)
#undef NBVM_COMPARE
                        inline uint32_t VectorIntAdd(uint32_t a, uint32_t b) {
    return a + b;
}
inline uint32_t VectorIntSubtract(uint32_t a, uint32_t b) {
    return a - b;
}
inline uint32_t VectorIntMultiply(uint32_t a, uint32_t b) {
    return a * b;
}
inline uint32_t VVMIntDiv(uint32_t a, uint32_t b) {
    if (!b)
        return 0;
    if (a == 0x80000000u && b == UINT32_MAX)
        return INT32_MAX;
    return uint32_t(signed_word(a) / signed_word(b));
}
inline uint32_t VectorIntMin(uint32_t a, uint32_t b) {
    return signed_word(a) < signed_word(b) ? a : b;
}
inline uint32_t VectorIntMax(uint32_t a, uint32_t b) {
    return signed_word(a) > signed_word(b) ? a : b;
}
inline uint32_t VectorIntClamp(uint32_t a, uint32_t b, uint32_t c) {
    return VectorIntMin(VectorIntMax(a, b), c);
}
inline uint32_t VectorIntAbs(uint32_t a) {
    return signed_word(a) < 0 ? 0u - a : a;
}
inline uint32_t VectorIntNegate(uint32_t a) {
    return 0u - a;
}
inline uint32_t VectorIntSign(uint32_t a) {
    return signed_word(a) < 0 ? UINT32_MAX : 1;
}
inline uint32_t VectorIntAnd(uint32_t a, uint32_t b) {
    return a & b;
}
inline uint32_t VectorIntOr(uint32_t a, uint32_t b) {
    return a | b;
}
inline uint32_t VectorIntXor(uint32_t a, uint32_t b) {
    return a ^ b;
}
inline uint32_t VectorIntNot(uint32_t a) {
    return ~a;
}
inline uint32_t VVMIntLShift(uint32_t a, uint32_t b) {
    return a << (b & 31);
}
inline uint32_t VVMIntRShift(uint32_t a, uint32_t b) {
    return a >> (b & 31);
}
inline uint32_t VectorIntToFloat(uint32_t a) {
    return bits(float(signed_word(a)));
}
inline uint32_t VectorFloatToInt(uint32_t a) {
    const auto f = number(a);
    return !std::isfinite(f) || f >= 2147483648.f || f < -2147483648.f ? 0x80000000u : uint32_t(int32_t(f));
}
inline uint32_t VVMf2i(uint32_t a) {
    return VectorFloatToInt(a);
}
inline uint32_t VVMi2f(uint32_t a) {
    return VectorIntToFloat(a);
}
inline uint32_t VectorReciprocalEstimate(uint32_t a) {
    return bits(_mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(number(a)))));
}
inline uint32_t VectorReciprocalSqrt(uint32_t a) {
    return bits(1.f / std::sqrt(number(a)));
}
inline uint32_t VectorSqrt(uint32_t a) {
    return bits(std::sqrt(number(a)));
}
inline uint32_t VectorExp(uint32_t a) {
    return bits(float(std::exp(double(number(a)))));
}
inline uint32_t VectorExp2(uint32_t a) {
    return bits(float(std::pow(2., double(number(a)))));
}
inline uint32_t VectorLog(uint32_t a) {
    return bits(float(std::log(double(number(a)))));
}
inline uint32_t VectorLog2(uint32_t a) {
    return bits(float(std::log(double(number(a))) / std::log(2.)));
}
inline float trig(float x, bool cosine) {
    if (std::abs(x) > 6.28319f)
        return float(cosine ? std::cos(double(x)) : std::sin(double(x)));
    float a = std::abs(x), shifted = cosine ? a + 1.5707963705062866f : a;
    float rounded = shifted * .31830987334251404f + 12582912.f;
    bool negative = (bits(rounded) & 1) != 0;
    float n = rounded - 12582912.f;
    if (cosine)
        n -= .5f;
    float r = a;
    for (float part : {3.140625f, .0009675025939941406f, 1.5099067240953445e-7f, 5.126688136514179e-12f})
        r -= n * part;
    float r2 = r * r;
    if (negative)
        r = -r;
    float p = 2.605780537123792e-6f;
    for (float c : {-.00019809602235909551f, .008333062753081322f, -.16666659712791443f})
        p = p * r2 + c;
    float correction = cosine ? p * (r2 * r) : (r2 * p) * r;
    float result = r + correction;
    if (cosine && shifted == 1.5707963705062866f)
        return 1.f;
    return x < 0 && !cosine ? -result : result;
}
inline uint32_t VectorSin(uint32_t a) {
    return bits(trig(number(a), false));
}
inline uint32_t VectorCos(uint32_t a) {
    return bits(trig(number(a), true));
}
inline uint32_t VectorTan(uint32_t a) {
    return bits(float(std::tan(double(number(a)))));
}
inline uint32_t VectorATan(uint32_t a) {
    return bits(float(std::atan(double(number(a)))));
}
inline uint32_t VectorATan2(uint32_t a, uint32_t b) {
    return bits(float(std::atan2(double(number(a)), double(number(b)))));
}
inline uint32_t VectorCeil(uint32_t a) {
    return bits(std::ceil(number(a)));
}
inline uint32_t VectorFloor(uint32_t a) {
    return bits(std::floor(number(a)));
}
inline uint32_t VectorTruncate(uint32_t a) {
    return bits(std::trunc(number(a)));
}
inline uint32_t VectorFractional(uint32_t a) {
    return VectorSubtract(a, VectorTruncate(a));
}
inline uint32_t VectorMod(uint32_t a, uint32_t b) {
    return bits(float(std::fmod(double(number(a)), double(number(b)))));
}
inline uint32_t VectorPow(uint32_t a, uint32_t b) {
    float x = number(a), y = number(b);
    if (x <= 1e-8f)
        return 0;
    if (!std::isfinite(x) || !std::isfinite(y))
        return bits(float(std::pow(double(x), double(y))));
    uint32_t raw = bits(x), fraction = raw & 0x7fffff, carry = (fraction + 0x4afb10) & 0x800000;
    float m = number(fraction | (0x3f800000 ^ carry));
    int e = int((raw >> 23) & 255) - 127 + int(carry >> 23);
    double estimate = number(VectorReciprocalEstimate(bits(1.f + m))), total = 1. + double(m);
    double reciprocal = estimate * (total * estimate - 2.),
           z = (total * reciprocal + 2.) * ((2. - total) * reciprocal), z2 = z * z;
    double p = .23801162294990488;
    for (double c : {.28524917668329924, .40000555065237864, .666666643542379, 2.})
        p = p * z2 + c;
    double t = ((z * 1.4426950408889634) * p + e) * y;
    if (t > 128.)
        return bits(INFINITY);
    if (t < -150.)
        return 0;
    double rounded = 6755399441055744. + t, r = t + (6755399441055744. - rounded);
    uint64_t u;
    std::memcpy(&u, &rounded, 8);
    u = ((u + 1010) << 52) | UINT64_C(0x41d33371366f6);
    double scale;
    std::memcpy(&scale, &u, 8);
    return bits(float(scale * ((((6.858109015731002 + r) * r + 13.207354074378483) *
                                ((4.186611958611718 + r) * r + 16.625772605936234)) *
                               ((-2.3127417210384476 + r) * r + 29.676485611755382))));
}
inline uint32_t VectorRound(uint32_t a) {
    float f = number(a);
    if (!std::isfinite(f))
        return a;
    double low = std::floor(double(f)), d = double(f) - low;
    return bits(float(low + ((d > .5 || (d == .5 && std::fmod(low, 2.) != 0)) ? 1. : 0.)));
}
inline uint32_t VectorSign(uint32_t a) {
    return bits(number(a) >= 0 ? 1.f : -1.f);
}
inline uint32_t VectorStep(uint32_t a) {
    return bits(number(a) >= 0 ? 1.f : 0.f);
}
} // namespace nbvm
