#pragma once

#include <cmath>
#include "types.hpp"

namespace riskdb {

struct OptionGreeks {
    double price{0.0};
    double delta{0.0};
    double gamma{0.0};
    double vega{0.0};
    double theta{0.0};
};

/**
 * @brief Standard normal probability density function: n(x) = (1 / sqrt(2*pi)) * exp(-0.5 * x^2)
 */
inline double normal_pdf(double x) noexcept {
    constexpr double INV_SQRT_2PI = 0.39894228040143267793994605993438;
    return INV_SQRT_2PI * std::exp(-0.5 * x * x);
}

/**
 * @brief Standard normal cumulative distribution function: N(x) = 0.5 * (1 + erf(x / sqrt(2)))
 */
inline double normal_cdf(double x) noexcept {
    constexpr double INV_SQRT_2 = 0.70710678118654752440084436210485;
    return 0.5 * std::erfc(-x * INV_SQRT_2);
}

/**
 * @brief Evaluates European Option Price and Greeks analytically via standard Black-Scholes formula.
 *
 * @param S Current spot price of the underlying asset (S > 0)
 * @param K Strike price (K > 0)
 * @param tau Time to maturity in years (tau > 0)
 * @param r Annualized risk-free interest rate (r >= 0)
 * @param sigma Annualized asset volatility (sigma > 0)
 * @param is_put False for Call option (0), True for Put option (1)
 * @return OptionGreeks Struct containing Price, Delta, Gamma, Vega, Theta
 */
OptionGreeks black_scholes_scalar(
    double S,
    double K,
    double tau,
    double r,
    double sigma,
    bool is_put
) noexcept;

/**
 * @brief Computes Option Price and Delta for a single RawOptionRecord.
 */
inline OptionGreeks compute_record_greeks(const RawOptionRecord& rec) noexcept {
    return black_scholes_scalar(
        static_cast<double>(rec.spot_price),
        static_cast<double>(rec.strike_price),
        static_cast<double>(rec.time_to_expiry),
        static_cast<double>(rec.risk_free_rate),
        static_cast<double>(rec.volatility),
        rec.option_type == 1
    );
}

} // namespace riskdb
