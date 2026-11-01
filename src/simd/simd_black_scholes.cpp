#include "simd/simd_black_scholes.hpp"
#include "simd/simd_math.hpp"
#include "common/math_utils.hpp"
#include <algorithm>

namespace riskdb {

void SIMDBlackScholesKernel::evaluate_batch(
    const ColumnarTable& table,
    size_t start_idx,
    size_t count,
    float* out_prices,
    float* out_deltas,
    float* out_gammas,
    float* out_vegas
) noexcept {
    const float* const __restrict S_arr     = table.spot_prices();
    const float* const __restrict K_arr     = table.strike_prices();
    const float* const __restrict tau_arr   = table.times_to_expiry();
    const float* const __restrict sig_arr   = table.volatilities();
    const float* const __restrict r_arr     = table.risk_free_rates();
    const uint8_t* const __restrict type_arr = table.option_types();

    size_t i = start_idx;
    const size_t end_idx = start_idx + count;

#if defined(__AVX2__)
    constexpr size_t VEC_WIDTH = 8;
    const size_t simd_end = start_idx + (count / VEC_WIDTH) * VEC_WIDTH;

    for (; i < simd_end; i += VEC_WIDTH) {
        __m256 S   = _mm256_load_ps(&S_arr[i]);
        __m256 K   = _mm256_load_ps(&K_arr[i]);
        __m256 tau = _mm256_load_ps(&tau_arr[i]);
        __m256 sig = _mm256_load_ps(&sig_arr[i]);
        __m256 r   = _mm256_load_ps(&r_arr[i]);

        __m256 sqrt_tau = _mm256_sqrt_ps(tau);
        __m256 sig_sqrt_tau = _mm256_mul_ps(sig, sqrt_tau);

        __m256 half_sig2 = _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_mul_ps(sig, sig));
        __m256 r_plus_half_sig2 = _mm256_add_ps(r, half_sig2);

        __m256 log_S_K = simd::log_avx2(_mm256_div_ps(S, K));
        __m256 d1_num = _mm256_fmadd_ps(r_plus_half_sig2, tau, log_S_K);
        __m256 d1 = _mm256_div_ps(d1_num, sig_sqrt_tau);
        __m256 d2 = _mm256_sub_ps(d1, sig_sqrt_tau);

        __m256 exp_neg_rt = simd::exp_avx2(_mm256_mul_ps(_mm256_set1_ps(-1.0f), _mm256_mul_ps(r, tau)));
        __m256 N_d1 = simd::normal_cdf_avx2(d1);
        __m256 N_d2 = simd::normal_cdf_avx2(d2);
        __m256 n_d1 = simd::normal_pdf_avx2(d1);

        __m256 call_price = _mm256_sub_ps(_mm256_mul_ps(S, N_d1), _mm256_mul_ps(K, _mm256_mul_ps(exp_neg_rt, N_d2)));
        __m256 call_delta = N_d1;

        __m256 neg_d1 = _mm256_sub_ps(_mm256_setzero_ps(), d1);
        __m256 neg_d2 = _mm256_sub_ps(_mm256_setzero_ps(), d2);
        __m256 N_neg_d1 = simd::normal_cdf_avx2(neg_d1);
        __m256 N_neg_d2 = simd::normal_cdf_avx2(neg_d2);

        __m256 put_price = _mm256_sub_ps(_mm256_mul_ps(K, _mm256_mul_ps(exp_neg_rt, N_neg_d2)), _mm256_mul_ps(S, N_neg_d1));
        __m256 put_delta = _mm256_sub_ps(N_d1, _mm256_set1_ps(1.0f));

        // Load 8 option types into 32-bit integer register, check == 1 (PUT)
        __m128i u8_types = _mm_loadu_si64(&type_arr[i]);
        __m256i i32_types = _mm256_cvtepu8_epi32(u8_types);
        __m256 is_put = _mm256_castsi256_ps(_mm256_cmpeq_epi32(i32_types, _mm256_set1_epi32(1)));

        __m256 price = _mm256_blendv_ps(call_price, put_price, is_put);
        __m256 delta = _mm256_blendv_ps(call_delta, put_delta, is_put);
        __m256 gamma = _mm256_div_ps(n_d1, _mm256_mul_ps(S, sig_sqrt_tau));
        __m256 vega  = _mm256_mul_ps(_mm256_mul_ps(S, sqrt_tau), n_d1);

        const size_t out_offset = i - start_idx;
        _mm256_storeu_ps(&out_prices[out_offset], price);
        _mm256_storeu_ps(&out_deltas[out_offset], delta);
        _mm256_storeu_ps(&out_gammas[out_offset], gamma);
        _mm256_storeu_ps(&out_vegas[out_offset],  vega);
    }

#elif defined(__ARM_NEON)
    constexpr size_t VEC_WIDTH = 4;
    const size_t simd_end = start_idx + (count / VEC_WIDTH) * VEC_WIDTH;

    for (; i < simd_end; i += VEC_WIDTH) {
        float32x4_t S   = vld1q_f32(&S_arr[i]);
        float32x4_t K   = vld1q_f32(&K_arr[i]);
        float32x4_t tau = vld1q_f32(&tau_arr[i]);
        float32x4_t sig = vld1q_f32(&sig_arr[i]);
        float32x4_t r   = vld1q_f32(&r_arr[i]);

        float32x4_t sqrt_tau = vsqrtq_f32(tau);
        float32x4_t sig_sqrt_tau = vmulq_f32(sig, sqrt_tau);

        float32x4_t half_sig2 = vmulq_f32(vdupq_n_f32(0.5f), vmulq_f32(sig, sig));
        float32x4_t r_plus_half_sig2 = vaddq_f32(r, half_sig2);

        float32x4_t log_S_K = simd::log_neon(vdivq_f32(S, K));
        float32x4_t d1_num = vfmaq_f32(log_S_K, r_plus_half_sig2, tau);
        float32x4_t d1 = vdivq_f32(d1_num, sig_sqrt_tau);
        float32x4_t d2 = vsubq_f32(d1, sig_sqrt_tau);

        float32x4_t exp_neg_rt = simd::exp_neon(vmulq_f32(vdupq_n_f32(-1.0f), vmulq_f32(r, tau)));
        float32x4_t N_d1 = simd::normal_cdf_neon(d1);
        float32x4_t N_d2 = simd::normal_cdf_neon(d2);
        float32x4_t n_d1 = simd::normal_pdf_neon(d1);

        float32x4_t call_price = vsubq_f32(vmulq_f32(S, N_d1), vmulq_f32(K, vmulq_f32(exp_neg_rt, N_d2)));
        float32x4_t call_delta = N_d1;

        float32x4_t neg_d1 = vnegq_f32(d1);
        float32x4_t neg_d2 = vnegq_f32(d2);
        float32x4_t N_neg_d1 = simd::normal_cdf_neon(neg_d1);
        float32x4_t N_neg_d2 = simd::normal_cdf_neon(neg_d2);

        float32x4_t put_price = vsubq_f32(vmulq_f32(K, vmulq_f32(exp_neg_rt, N_neg_d2)), vmulq_f32(S, N_neg_d1));
        float32x4_t put_delta = vsubq_f32(N_d1, vdupq_n_f32(1.0f));

        uint8x8_t u8_types = vld1_u8(&type_arr[i]);
        uint16x8_t u16_types = vmovl_u8(u8_types);
        uint32x4_t u32_types = vmovl_u16(vget_low_u16(u16_types));
        uint32x4_t is_put = vceqq_u32(u32_types, vdupq_n_u32(1));

        float32x4_t price = vbslq_f32(is_put, put_price, call_price);
        float32x4_t delta = vbslq_f32(is_put, put_delta, call_delta);
        float32x4_t gamma = vdivq_f32(n_d1, vmulq_f32(S, sig_sqrt_tau));
        float32x4_t vega  = vmulq_f32(vmulq_f32(S, sqrt_tau), n_d1);

        const size_t out_offset = i - start_idx;
        vst1q_f32(&out_prices[out_offset], price);
        vst1q_f32(&out_deltas[out_offset], delta);
        vst1q_f32(&out_gammas[out_offset], gamma);
        vst1q_f32(&out_vegas[out_offset],  vega);
    }
#endif

    // Scalar fallback for remaining tail elements
    for (; i < end_idx; ++i) {
        OptionGreeks g = black_scholes_scalar(
            S_arr[i], K_arr[i], tau_arr[i], r_arr[i], sig_arr[i], type_arr[i] == 1
        );
        const size_t out_offset = i - start_idx;
        out_prices[out_offset] = static_cast<float>(g.price);
        out_deltas[out_offset] = static_cast<float>(g.delta);
        out_gammas[out_offset] = static_cast<float>(g.gamma);
        out_vegas[out_offset]  = static_cast<float>(g.vega);
    }
}

void SIMDBlackScholesKernel::evaluate_and_accumulate_chunk(
    const ColumnarTable& table,
    size_t start_idx,
    size_t count,
    double* local_portfolio_deltas,
    double* local_portfolio_values,
    double* local_portfolio_gammas,
    double* local_portfolio_vegas,
    uint64_t* local_portfolio_counts,
    size_t num_portfolios
) noexcept {
    const float* const __restrict S_arr       = table.spot_prices();
    const float* const __restrict K_arr       = table.strike_prices();
    const float* const __restrict tau_arr     = table.times_to_expiry();
    const float* const __restrict sig_arr     = table.volatilities();
    const float* const __restrict r_arr       = table.risk_free_rates();
    const float* const __restrict qty_arr     = table.quantities();
    const uint8_t* const __restrict type_arr   = table.option_types();
    const uint16_t* const __restrict port_arr  = table.portfolio_ids();

    size_t i = start_idx;
    const size_t end_idx = start_idx + count;

#if defined(__AVX2__)
    constexpr size_t VEC_WIDTH = 8;
    const size_t simd_end = start_idx + (count / VEC_WIDTH) * VEC_WIDTH;

    alignas(32) float prices[VEC_WIDTH];
    alignas(32) float deltas[VEC_WIDTH];
    alignas(32) float gammas[VEC_WIDTH];
    alignas(32) float vegas[VEC_WIDTH];

    for (; i < simd_end; i += VEC_WIDTH) {
        __m256 S   = _mm256_load_ps(&S_arr[i]);
        __m256 K   = _mm256_load_ps(&K_arr[i]);
        __m256 tau = _mm256_load_ps(&tau_arr[i]);
        __m256 sig = _mm256_load_ps(&sig_arr[i]);
        __m256 r   = _mm256_load_ps(&r_arr[i]);
        __m256 qty = _mm256_load_ps(&qty_arr[i]);

        __m256 sqrt_tau = _mm256_sqrt_ps(tau);
        __m256 sig_sqrt_tau = _mm256_mul_ps(sig, sqrt_tau);

        __m256 half_sig2 = _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_mul_ps(sig, sig));
        __m256 r_plus_half_sig2 = _mm256_add_ps(r, half_sig2);

        __m256 log_S_K = simd::log_avx2(_mm256_div_ps(S, K));
        __m256 d1_num = _mm256_fmadd_ps(r_plus_half_sig2, tau, log_S_K);
        __m256 d1 = _mm256_div_ps(d1_num, sig_sqrt_tau);
        __m256 d2 = _mm256_sub_ps(d1, sig_sqrt_tau);

        __m256 exp_neg_rt = simd::exp_avx2(_mm256_mul_ps(_mm256_set1_ps(-1.0f), _mm256_mul_ps(r, tau)));
        __m256 N_d1 = simd::normal_cdf_avx2(d1);
        __m256 N_d2 = simd::normal_cdf_avx2(d2);
        __m256 n_d1 = simd::normal_pdf_avx2(d1);

        __m256 call_price = _mm256_sub_ps(_mm256_mul_ps(S, N_d1), _mm256_mul_ps(K, _mm256_mul_ps(exp_neg_rt, N_d2)));
        __m256 call_delta = N_d1;

        __m256 neg_d1 = _mm256_sub_ps(_mm256_setzero_ps(), d1);
        __m256 neg_d2 = _mm256_sub_ps(_mm256_setzero_ps(), d2);
        __m256 N_neg_d1 = simd::normal_cdf_avx2(neg_d1);
        __m256 N_neg_d2 = simd::normal_cdf_avx2(neg_d2);

        __m256 put_price = _mm256_sub_ps(_mm256_mul_ps(K, _mm256_mul_ps(exp_neg_rt, N_neg_d2)), _mm256_mul_ps(S, N_neg_d1));
        __m256 put_delta = _mm256_sub_ps(N_d1, _mm256_set1_ps(1.0f));

        __m128i u8_types = _mm_loadu_si64(&type_arr[i]);
        __m256i i32_types = _mm256_cvtepu8_epi32(u8_types);
        __m256 is_put = _mm256_castsi256_ps(_mm256_cmpeq_epi32(i32_types, _mm256_set1_epi32(1)));

        __m256 v_price = _mm256_mul_ps(qty, _mm256_blendv_ps(call_price, put_price, is_put));
        __m256 v_delta = _mm256_mul_ps(qty, _mm256_blendv_ps(call_delta, put_delta, is_put));
        __m256 v_gamma = _mm256_mul_ps(qty, _mm256_div_ps(n_d1, _mm256_mul_ps(S, sig_sqrt_tau)));
        __m256 v_vega  = _mm256_mul_ps(qty, _mm256_mul_ps(_mm256_mul_ps(S, sqrt_tau), n_d1));

        _mm256_store_ps(prices, v_price);
        _mm256_store_ps(deltas, v_delta);
        _mm256_store_ps(gammas, v_gamma);
        _mm256_store_ps(vegas,  v_vega);

        for (size_t lane = 0; lane < VEC_WIDTH; ++lane) {
            const uint16_t pid = port_arr[i + lane];
            if (pid < num_portfolios) {
                local_portfolio_values[pid]  += prices[lane];
                local_portfolio_deltas[pid]  += deltas[lane];
                local_portfolio_gammas[pid]  += gammas[lane];
                local_portfolio_vegas[pid]   += vegas[lane];
                local_portfolio_counts[pid]  += 1;
            }
        }
    }

#elif defined(__ARM_NEON)
    constexpr size_t VEC_WIDTH = 4;
    const size_t simd_end = start_idx + (count / VEC_WIDTH) * VEC_WIDTH;

    alignas(16) float prices[VEC_WIDTH];
    alignas(16) float deltas[VEC_WIDTH];
    alignas(16) float gammas[VEC_WIDTH];
    alignas(16) float vegas[VEC_WIDTH];

    for (; i < simd_end; i += VEC_WIDTH) {
        float32x4_t S   = vld1q_f32(&S_arr[i]);
        float32x4_t K   = vld1q_f32(&K_arr[i]);
        float32x4_t tau = vld1q_f32(&tau_arr[i]);
        float32x4_t sig = vld1q_f32(&sig_arr[i]);
        float32x4_t r   = vld1q_f32(&r_arr[i]);
        float32x4_t qty = vld1q_f32(&qty_arr[i]);

        float32x4_t sqrt_tau = vsqrtq_f32(tau);
        float32x4_t sig_sqrt_tau = vmulq_f32(sig, sqrt_tau);

        float32x4_t half_sig2 = vmulq_f32(vdupq_n_f32(0.5f), vmulq_f32(sig, sig));
        float32x4_t r_plus_half_sig2 = vaddq_f32(r, half_sig2);

        float32x4_t log_S_K = simd::log_neon(vdivq_f32(S, K));
        float32x4_t d1_num = vfmaq_f32(log_S_K, r_plus_half_sig2, tau);
        float32x4_t d1 = vdivq_f32(d1_num, sig_sqrt_tau);
        float32x4_t d2 = vsubq_f32(d1, sig_sqrt_tau);

        float32x4_t exp_neg_rt = simd::exp_neon(vmulq_f32(vdupq_n_f32(-1.0f), vmulq_f32(r, tau)));
        float32x4_t N_d1 = simd::normal_cdf_neon(d1);
        float32x4_t N_d2 = simd::normal_cdf_neon(d2);
        float32x4_t n_d1 = simd::normal_pdf_neon(d1);

        float32x4_t call_price = vsubq_f32(vmulq_f32(S, N_d1), vmulq_f32(K, vmulq_f32(exp_neg_rt, N_d2)));
        float32x4_t call_delta = N_d1;

        float32x4_t neg_d1 = vnegq_f32(d1);
        float32x4_t neg_d2 = vnegq_f32(d2);
        float32x4_t N_neg_d1 = simd::normal_cdf_neon(neg_d1);
        float32x4_t N_neg_d2 = simd::normal_cdf_neon(neg_d2);

        float32x4_t put_price = vsubq_f32(vmulq_f32(K, vmulq_f32(exp_neg_rt, N_neg_d2)), vmulq_f32(S, N_neg_d1));
        float32x4_t put_delta = vsubq_f32(N_d1, vdupq_n_f32(1.0f));

        uint8x8_t u8_types = vld1_u8(&type_arr[i]);
        uint16x8_t u16_types = vmovl_u8(u8_types);
        uint32x4_t u32_types = vmovl_u16(vget_low_u16(u16_types));
        uint32x4_t is_put = vceqq_u32(u32_types, vdupq_n_u32(1));

        float32x4_t v_price = vmulq_f32(qty, vbslq_f32(is_put, put_price, call_price));
        float32x4_t v_delta = vmulq_f32(qty, vbslq_f32(is_put, put_delta, call_delta));
        float32x4_t v_gamma = vmulq_f32(qty, vdivq_f32(n_d1, vmulq_f32(S, sig_sqrt_tau)));
        float32x4_t v_vega  = vmulq_f32(qty, vmulq_f32(vmulq_f32(S, sqrt_tau), n_d1));

        vst1q_f32(prices, v_price);
        vst1q_f32(deltas, v_delta);
        vst1q_f32(gammas, v_gamma);
        vst1q_f32(vegas,  v_vega);

        for (size_t lane = 0; lane < VEC_WIDTH; ++lane) {
            const uint16_t pid = port_arr[i + lane];
            if (pid < num_portfolios) {
                local_portfolio_values[pid]  += prices[lane];
                local_portfolio_deltas[pid]  += deltas[lane];
                local_portfolio_gammas[pid]  += gammas[lane];
                local_portfolio_vegas[pid]   += vegas[lane];
                local_portfolio_counts[pid]  += 1;
            }
        }
    }
#endif

    // Scalar fallback for tail elements
    for (; i < end_idx; ++i) {
        OptionGreeks g = black_scholes_scalar(
            S_arr[i], K_arr[i], tau_arr[i], r_arr[i], sig_arr[i], type_arr[i] == 1
        );
        const uint16_t pid = port_arr[i];
        if (pid < num_portfolios) {
            const double q = qty_arr[i];
            local_portfolio_values[pid]  += g.price * q;
            local_portfolio_deltas[pid]  += g.delta * q;
            local_portfolio_gammas[pid]  += g.gamma * q;
            local_portfolio_vegas[pid]   += g.vega * q;
            local_portfolio_counts[pid]  += 1;
        }
    }
}

} // namespace riskdb
