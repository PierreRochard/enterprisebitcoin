#ifndef BITCOIN_ENTERPRISE_SPENT_AGE_H
#define BITCOIN_ENTERPRISE_SPENT_AGE_H

#include <consensus/amount.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace enterprise {

//! Lower edge, in days, of each band in blocks.spent_age_bands. A year is 365
//! days. The 155-day edge splits short-term from long-term holders.
inline constexpr std::array<int64_t, 14> SPENT_AGE_BAND_MIN_DAYS{
    0, 1, 7, 30, 90, 155, 180, 365, 730, 1095, 1825, 2555, 3650, 5475};

//! Index into SPENT_AGE_BAND_MIN_DAYS of the band an age falls into. Negative
//! ages count as zero.
[[nodiscard]] std::size_t SpentAgeBand(int64_t age_seconds);

//! Age and value of every coin a block spends.
class SpentAgeStats
{
public:
    void Add(CAmount value_sats, int created_height, int64_t age_seconds);

    //! Sum over spent coins of value (BTC) times age (days).
    [[nodiscard]] double CoinDaysDestroyed() const { return m_coin_days_destroyed; }
    //! Height the oldest spent coin was created at; empty when nothing is spent.
    [[nodiscard]] std::optional<int> OldestSpentHeight() const { return m_oldest_spent_height; }
    //! Non-empty bands as [[min_days, inputs, value_sats], ...], youngest first.
    [[nodiscard]] std::string BandsJson() const;

private:
    struct Band {
        int64_t inputs{0};
        CAmount value_sats{0};
    };

    std::array<Band, SPENT_AGE_BAND_MIN_DAYS.size()> m_bands{};
    double m_coin_days_destroyed{0.0};
    std::optional<int> m_oldest_spent_height;
};

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_SPENT_AGE_H
