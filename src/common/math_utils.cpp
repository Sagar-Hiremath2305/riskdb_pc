#include "common/math_utils.hpp"
#include <algorithm>

namespace riskdb {

OptionGreeks black_scholes_scalar(
    double S,
    double K,
    double tau,
    double r,
    double sigma,
    bool is_put
) noexcept {
    OptionGreeks greeks;
    if (S <= 0.0 || K <= 0.0 || tau <= 0.0 || sigma <= 0.0) {
        return greeks;
    }

    const double sqrt_tau = std::sqrt(tau);
    const double sigma_sqrt_tau = sigma * sqrt_tau;
    const double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * tau) / sigma_sqrt_tau;
    const double d2 = d1 - sigma_sqrt_tau;

    const double exp_neg_rt = std::exp(-r * tau);
    const double N_d1 = normal_cdf(d1);
    const double N_d2 = normal_cdf(d2);
    const double n_d1 = normal_pdf(d1);

    if (!is_put) {
        // Call option
        greeks.price = S * N_d1 - K * exp_neg_rt * N_d2;
        greeks.delta = N_d1;
        greeks.theta = -(S * sigma * n_d1) / (2.0 * sqrt_tau) - r * K * exp_neg_rt * N_d2;
    } else {
        // Put option
        const double N_neg_d1 = normal_cdf(-d1);
        const double N_neg_d2 = normal_cdf(-d2);
        greeks.price = K * exp_neg_rt * N_neg_d2 - S * N_neg_d1;
        greeks.delta = N_d1 - 1.0; // or -N(-d1)
        greeks.theta = -(S * sigma * n_d1) / (2.0 * sqrt_tau) + r * K * exp_neg_rt * N_neg_d2;
    }

    greeks.gamma = n_d1 / (S * sigma_sqrt_tau);
    greeks.vega  = S * sqrt_tau * n_d1;

    return greeks;
}

} // namespace riskdb
