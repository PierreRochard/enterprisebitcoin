#include <enterprise/denomination_classifier.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace enterprise {
namespace {

constexpr double HIGH_SCORE{0.70};
constexpr double MIN_WINNING_DELTA{0.15};
constexpr double SATS_PER_BTC{static_cast<double>(COIN)};

bool IsNiceMantissa(CAmount value)
{
    static constexpr std::array<CAmount, 8> NICE_MANTISSAS{{1, 2, 5, 10, 20, 25, 50, 100}};
    return std::find(NICE_MANTISSAS.begin(), NICE_MANTISSAS.end(), value) != NICE_MANTISSAS.end();
}

double PriceWindowScore(double implied_price, const DenominationPriceWindow& window)
{
    if (!std::isfinite(implied_price) || implied_price <= 0.0 || !window.Valid()) return 0.0;
    if (implied_price < window.low || implied_price > window.high) return 0.0;

    const double midpoint{(window.low + window.high) / 2.0};
    const double half_span{(window.high - window.low) / 2.0};
    if (half_span <= 0.0) {
        const double tolerance{std::max(1.0, midpoint) * 1e-9};
        return std::abs(implied_price - midpoint) <= tolerance ? 1.0 : 0.0;
    }

    const double normalized_distance{std::min(1.0, std::abs(implied_price - midpoint) / half_span)};
    return 1.0 - (0.20 * normalized_distance);
}

double UsdDenominationScore(CAmount nvalue_sats, const DenominationPriceWindow& window)
{
    if (nvalue_sats <= 0) return 0.0;

    double best_score{0.0};
    auto consider_target_cents = [&](int64_t target_cents) {
        if (target_cents <= 0) return;
        const double target_usd{static_cast<double>(target_cents) / 100.0};
        const double implied_price{target_usd * SATS_PER_BTC / static_cast<double>(nvalue_sats)};
        best_score = std::max(best_score, PriceWindowScore(implied_price, window));
    };

    static constexpr std::array<int64_t, 9> COMMON_BILLS{{1, 2, 5, 10, 20, 50, 100, 500, 1000}};
    for (const int64_t dollars : COMMON_BILLS) {
        consider_target_cents(dollars * 100);
    }
    for (int64_t dollars{1}; dollars <= 1000; ++dollars) {
        consider_target_cents(dollars * 100);
    }
    for (int64_t dollars{1}; dollars <= 100; ++dollars) {
        consider_target_cents(dollars * 100 + 95);
        consider_target_cents(dollars * 100 + 99);
    }

    return best_score;
}

double SatsDenominationScore(CAmount nvalue_sats)
{
    if (nvalue_sats <= 0) return 0.0;

    double score{0.0};
    if (nvalue_sats == COIN || nvalue_sats == COIN / 10 || nvalue_sats == COIN / 100 || nvalue_sats == COIN / 1000) {
        score = std::max(score, 1.0);
    }

    if (nvalue_sats % 1'000'000 == 0) {
        score = std::max(score, 0.95);
    } else if (nvalue_sats % 100'000 == 0) {
        score = std::max(score, 0.90);
    } else if (nvalue_sats % 10'000 == 0) {
        score = std::max(score, 0.80);
    } else if (nvalue_sats % 1'000 == 0) {
        score = std::max(score, 0.70);
    }

    CAmount mantissa{nvalue_sats};
    int trailing_zeroes{0};
    while (mantissa > 0 && mantissa % 10 == 0) {
        mantissa /= 10;
        ++trailing_zeroes;
    }
    if (trailing_zeroes > 0 && IsNiceMantissa(mantissa)) {
        score = std::max(score, std::min(0.95, 0.70 + (0.04 * trailing_zeroes)));
    }
    if (trailing_zeroes >= 3) {
        score = std::max(score, std::min(0.97, 0.65 + (0.08 * (trailing_zeroes - 2))));
    }

    return score;
}

} // namespace

bool DenominationPriceWindow::Valid() const
{
    return std::isfinite(price) && std::isfinite(low) && std::isfinite(high) &&
           price > 0.0 && low > 0.0 && high > 0.0 && low <= high;
}

DenominationResult ClassifyOutputDenomination(CAmount nvalue_sats, const std::optional<DenominationPriceWindow>& price_window)
{
    DenominationResult result;
    if (nvalue_sats <= 0 || !price_window || !price_window->Valid()) return result;

    result.usd_score = UsdDenominationScore(nvalue_sats, *price_window);
    result.sats_score = SatsDenominationScore(nvalue_sats);

    const bool usd_high{result.usd_score >= HIGH_SCORE};
    const bool sats_high{result.sats_score >= HIGH_SCORE};
    if (usd_high && sats_high) {
        result.category = DenominationCategory::AMBIGUOUS;
        return result;
    }
    if (usd_high && result.usd_score - result.sats_score >= MIN_WINNING_DELTA) {
        result.category = DenominationCategory::USD;
        result.confidence = result.usd_score;
        return result;
    }
    if (sats_high && result.sats_score - result.usd_score >= MIN_WINNING_DELTA) {
        result.category = DenominationCategory::SATS;
        result.confidence = result.sats_score;
        return result;
    }

    return result;
}

} // namespace enterprise
