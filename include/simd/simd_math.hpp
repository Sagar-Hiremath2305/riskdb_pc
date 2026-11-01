#pragma once

#include <cmath>
#include <cstdint>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace riskdb {
namespace simd {

// ============================================================================
// Scalar reference constants
// ============================================================================
constexpr float INV_SQRT_2PI = 0.3989422804014327f; // 1 / sqrt(2 * pi)
constexpr float SQRT_2 = 1.4142135623730951f;
constexpr float A1 =  0.319381530f;
constexpr float A2 = -0.356563782f;
constexpr float A3 =  1.781477937f;
constexpr float A4 = -1.821255978f;
constexpr float A5 =  1.330274429f;
constexpr float P_CONST = 0.2316419f;

#if defined(__AVX2__)
// ============================================================================
// AVX2 Vectorized Math (8 x 32-bit float per __m256)
// ============================================================================

/**
 * @brief Fast vectorized exponential approximation using Estrin/Horner polynomial on AVX2.
 */
inline __m256 exp_avx2(__m256 x) {
    // Clamp to avoid underflow/overflow: [-87.0f, 87.0f]
    x = _mm256_max_ps(x, _mm256_set1_ps(-87.3365f));
    x = _mm256_min_ps(x, _mm256_set1_ps(87.3365f));

    // Express exp(x) = 2^(k) * exp(r) where k = round(x * log2(e)), r = x - k * ln(2)
    const __m256 log2e = _mm256_set1_ps(1.4426950408889634f);
    const __m256 ln2_hi = _mm256_set1_ps(0.6931471805599453f);
    const __m256 c0 = _mm256_set1_ps(1.0f);
    const __m256 c1 = _mm256_set1_ps(1.0f);
    const __m256 c2 = _mm256_set1_ps(0.5f);
    const __m256 c3 = _mm256_set1_ps(0.16666666666666666f);
    const __m256 c4 = _mm256_set1_ps(0.041666666666666664f);
    const __m256 c5 = _mm256_set1_ps(0.008333333333333333f);

    __m256 fx = _mm256_round_ps(_mm256_mul_ps(x, log2e), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    __m256 r = _mm256_fnmadd_ps(fx, ln2_hi, x); // r = x - fx * ln2

    // Evaluate polynomial P(r) = 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120 via Horner
    __m256 poly = _mm256_fmadd_ps(c5, r, c4);
    poly = _mm256_fmadd_ps(poly, r, c3);
    poly = _mm256_fmadd_ps(poly, r, c2);
    poly = _mm256_fmadd_ps(poly, r, c1);
    poly = _mm256_fmadd_ps(poly, r, c0);

    // Compute 2^k via bit manipulation on the IEEE 754 exponent field
    __m256i k_int = _mm256_cvtps_epi32(fx);
    __m256i bias = _mm256_set1_epi32(127);
    __m256i exp_bits = _mm256_slli_epi32(_mm256_add_epi32(k_int, bias), 23);
    __m256 scale = _mm256_castsi256_ps(exp_bits);

    return _mm256_mul_ps(poly, scale);
}

/**
 * @brief Fast vectorized natural logarithm approximation on AVX2.
 */
inline __m256 log_avx2(__m256 x) {
    // Extract exponent and mantissa: x = m * 2^e with m in [1.0, 2.0)
    __m256i xi = _mm256_castps_si256(x);
    __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
    __m256i mant_mask = _mm256_set1_epi32(0x007FFFFF);
    __m256i one_bits = _mm256_set1_epi32(0x3F800000);

    __m256i e_bits = _mm256_sub_epi32(_mm256_srli_epi32(_mm256_and_si256(xi, exp_mask), 23), _mm256_set1_epi32(127));
    __m256 e = _mm256_cvtepi32_ps(e_bits);

    __m256 m = _mm256_castsi256_ps(_mm256_or_si256(_mm256_and_si256(xi, mant_mask), one_bits));

    // Map m to range [1/sqrt(2), sqrt(2)]
    __m256 mask = _mm256_cmp_ps(m, _mm256_set1_ps(1.41421356237f), _CMP_GT_OQ);
    m = _mm256_blendv_ps(m, _mm256_mul_ps(m, _mm256_set1_ps(0.5f)), mask);
    e = _mm256_add_ps(e, _mm256_and_ps(mask, _mm256_set1_ps(1.0f)));

    // y = (m - 1) / (m + 1)
    __m256 num = _mm256_sub_ps(m, _mm256_set1_ps(1.0f));
    __m256 den = _mm256_add_ps(m, _mm256_set1_ps(1.0f));
    __m256 y = _mm256_div_ps(num, den);
    __m256 y2 = _mm256_mul_ps(y, y);

    // Rational series: ln((1+y)/(1-y)) = 2 * y * (1 + y^2/3 + y^4/5 + y^6/7)
    __m256 poly = _mm256_fmadd_ps(y2, _mm256_set1_ps(0.2857142857f), _mm256_set1_ps(0.4000000000f));
    poly = _mm256_fmadd_ps(poly, y2, _mm256_set1_ps(0.6666666667f));
    poly = _mm256_fmadd_ps(poly, y2, _mm256_set1_ps(2.0000000000f));
    __m256 log_m = _mm256_mul_ps(y, poly);

    const __m256 ln2 = _mm256_set1_ps(0.69314718056f);
    return _mm256_fmadd_ps(e, ln2, log_m);
}

/**
 * @brief Vectorized Cumulative Normal Distribution Function N(x) using Abramowitz & Stegun 7.1.26.
 * Maximum relative error < 7.5e-8.
 */
inline __m256 normal_cdf_avx2(__m256 x) {
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one  = _mm256_set1_ps(1.0f);
    const __m256 sign_mask = _mm256_set1_ps(-0.0f);

    // Absolute value: abs_x = |x|
    __m256 abs_x = _mm256_andnot_ps(sign_mask, x);

    // t = 1.0f / (1.0f + P_CONST * abs_x)
    __m256 p = _mm256_set1_ps(P_CONST);
    __m256 t = _mm256_div_ps(one, _mm256_fmadd_ps(p, abs_x, one));

    // poly(t) = ((((A5 * t + A4) * t + A3) * t + A2) * t + A1) * t
    __m256 poly = _mm256_fmadd_ps(_mm256_set1_ps(A5), t, _mm256_set1_ps(A4));
    poly = _mm256_fmadd_ps(poly, t, _mm256_set1_ps(A3));
    poly = _mm256_fmadd_ps(poly, t, _mm256_set1_ps(A2));
    poly = _mm256_fmadd_ps(poly, t, _mm256_set1_ps(A1));
    poly = _mm256_mul_ps(poly, t);

    // Normal PDF n(x) = INV_SQRT_2PI * exp(-0.5 * x^2)
    __m256 x2 = _mm256_mul_ps(abs_x, abs_x);
    __m256 neg_half_x2 = _mm256_mul_ps(x2, _mm256_set1_ps(-0.5f));
    __m256 n_x = _mm256_mul_ps(_mm256_set1_ps(INV_SQRT_2PI), exp_avx2(neg_half_x2));

    // cdf_pos = 1.0 - n_x * poly
    __m256 cdf_pos = _mm256_fnmadd_ps(n_x, poly, one);
    // cdf_neg = n_x * poly
    __m256 cdf_neg = _mm256_mul_ps(n_x, poly);

    // If x >= 0 use cdf_pos, else use cdf_neg
    __m256 is_nonneg = _mm256_cmp_ps(x, zero, _CMP_GE_OQ);
    return _mm256_blendv_ps(cdf_neg, cdf_pos, is_nonneg);
}

/**
 * @brief Vectorized Normal PDF n(x) on AVX2.
 */
inline __m256 normal_pdf_avx2(__m256 x) {
    __m256 x2 = _mm256_mul_ps(x, x);
    __m256 neg_half_x2 = _mm256_mul_ps(x2, _mm256_set1_ps(-0.5f));
    return _mm256_mul_ps(_mm256_set1_ps(INV_SQRT_2PI), exp_avx2(neg_half_x2));
}

#elif defined(__ARM_NEON)
// ============================================================================
// ARM NEON Vectorized Math (4 x 32-bit float per float32x4_t)
// ============================================================================

inline float32x4_t exp_neon(float32x4_t x) {
    // Clamp to avoid underflow/overflow: [-87.0f, 87.0f]
    x = vmaxq_f32(x, vdupq_n_f32(-87.3365f));
    x = vminq_f32(x, vdupq_n_f32(87.3365f));

    float32x4_t log2e = vdupq_n_f32(1.4426950408889634f);
    float32x4_t ln2 = vdupq_n_f32(0.6931471805599453f);

    float32x4_t fx = vrndnq_f32(vmulq_f32(x, log2e));
    float32x4_t r = vmlsq_f32(x, fx, ln2);

    float32x4_t c0 = vdupq_n_f32(1.0f);
    float32x4_t c1 = vdupq_n_f32(1.0f);
    float32x4_t c2 = vdupq_n_f32(0.5f);
    float32x4_t c3 = vdupq_n_f32(0.16666666666666666f);
    float32x4_t c4 = vdupq_n_f32(0.041666666666666664f);
    float32x4_t c5 = vdupq_n_f32(0.008333333333333333f);

    float32x4_t poly = vfmaq_f32(c4, c5, r);
    poly = vfmaq_f32(c3, poly, r);
    poly = vfmaq_f32(c2, poly, r);
    poly = vfmaq_f32(c1, poly, r);
    poly = vfmaq_f32(c0, poly, r);

    int32x4_t k_int = vcvtq_s32_f32(fx);
    int32x4_t bias = vdupq_n_s32(127);
    int32x4_t exp_bits = vshlq_n_s32(vaddq_s32(k_int, bias), 23);
    float32x4_t scale = vreinterpretq_f32_s32(exp_bits);

    return vmulq_f32(poly, scale);
}

inline float32x4_t log_neon(float32x4_t x) {
    int32x4_t xi = vreinterpretq_s32_f32(x);
    int32x4_t exp_mask = vdupq_n_s32(0x7F800000);
    int32x4_t mant_mask = vdupq_n_s32(0x007FFFFF);
    int32x4_t one_bits = vdupq_n_s32(0x3F800000);

    int32x4_t e_bits = vsubq_s32(vshrq_n_s32(vandq_s32(xi, exp_mask), 23), vdupq_n_s32(127));
    float32x4_t e = vcvtq_f32_s32(e_bits);

    float32x4_t m = vreinterpretq_f32_s32(vorrq_s32(vandq_s32(xi, mant_mask), one_bits));

    uint32x4_t mask = vcgtq_f32(m, vdupq_n_f32(1.41421356237f));
    m = vbslq_f32(mask, vmulq_f32(m, vdupq_n_f32(0.5f)), m);
    e = vaddq_f32(e, vreinterpretq_f32_u32(vandq_u32(mask, vreinterpretq_u32_f32(vdupq_n_f32(1.0f)))));

    float32x4_t num = vsubq_f32(m, vdupq_n_f32(1.0f));
    float32x4_t den = vaddq_f32(m, vdupq_n_f32(1.0f));
    float32x4_t y = vdivq_f32(num, den);
    float32x4_t y2 = vmulq_f32(y, y);

    float32x4_t poly = vfmaq_f32(vdupq_n_f32(0.4000000000f), y2, vdupq_n_f32(0.2857142857f));
    poly = vfmaq_f32(vdupq_n_f32(0.6666666667f), y2, poly);
    poly = vfmaq_f32(vdupq_n_f32(2.0000000000f), y2, poly);
    float32x4_t log_m = vmulq_f32(y, poly);

    return vfmaq_f32(log_m, e, vdupq_n_f32(0.69314718056f));
}

inline float32x4_t normal_cdf_neon(float32x4_t x) {
    float32x4_t zero = vdupq_n_f32(0.0f);
    float32x4_t one  = vdupq_n_f32(1.0f);
    float32x4_t abs_x = vabsq_f32(x);

    float32x4_t t = vdivq_f32(one, vfmaq_f32(one, vdupq_n_f32(P_CONST), abs_x));

    float32x4_t poly = vfmaq_f32(vdupq_n_f32(A4), vdupq_n_f32(A5), t);
    poly = vfmaq_f32(vdupq_n_f32(A3), poly, t);
    poly = vfmaq_f32(vdupq_n_f32(A2), poly, t);
    poly = vfmaq_f32(vdupq_n_f32(A1), poly, t);
    poly = vmulq_f32(poly, t);

    float32x4_t x2 = vmulq_f32(abs_x, abs_x);
    float32x4_t neg_half_x2 = vmulq_f32(x2, vdupq_n_f32(-0.5f));
    float32x4_t n_x = vmulq_f32(vdupq_n_f32(INV_SQRT_2PI), exp_neon(neg_half_x2));

    float32x4_t cdf_pos = vsubq_f32(one, vmulq_f32(n_x, poly));
    float32x4_t cdf_neg = vmulq_f32(n_x, poly);

    uint32x4_t is_nonneg = vcgeq_f32(x, zero);
    return vbslq_f32(is_nonneg, cdf_pos, cdf_neg);
}

inline float32x4_t normal_pdf_neon(float32x4_t x) {
    float32x4_t x2 = vmulq_f32(x, x);
    float32x4_t neg_half_x2 = vmulq_f32(x2, vdupq_n_f32(-0.5f));
    return vmulq_f32(vdupq_n_f32(INV_SQRT_2PI), exp_neon(neg_half_x2));
}

#endif

} // namespace simd
} // namespace riskdb
