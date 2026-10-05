#include <enterprise/spent_age.h>

#include <algorithm>
#include <sstream>

namespace enterprise {

namespace {
constexpr int64_t SECONDS_PER_DAY{24 * 60 * 60};
} // namespace

std::size_t SpentAgeBand(int64_t age_seconds)
{
    const int64_t age_days{std::max<int64_t>(0, age_seconds) / SECONDS_PER_DAY};
    const auto above{std::upper_bound(SPENT_AGE_BAND_MIN_DAYS.begin(), SPENT_AGE_BAND_MIN_DAYS.end(), age_days)};
    return static_cast<std::size_t>(above - SPENT_AGE_BAND_MIN_DAYS.begin()) - 1;
}

void SpentAgeStats::Add(CAmount value_sats, int created_height, int64_t age_seconds)
{
    const int64_t age{std::max<int64_t>(0, age_seconds)};
    Band& band{m_bands[SpentAgeBand(age)]};
    band.inputs += 1;
    band.value_sats += value_sats;
    m_coin_days_destroyed += static_cast<double>(value_sats) / COIN * static_cast<double>(age) / SECONDS_PER_DAY;
    if (!m_oldest_spent_height || created_height < *m_oldest_spent_height) {
        m_oldest_spent_height = created_height;
    }
}

std::string SpentAgeStats::BandsJson() const
{
    std::ostringstream json;
    json << "[";
    bool first{true};
    for (std::size_t i = 0; i < m_bands.size(); ++i) {
        if (m_bands[i].inputs == 0) continue;
        if (!first) json << ",";
        first = false;
        json << "[" << SPENT_AGE_BAND_MIN_DAYS[i] << "," << m_bands[i].inputs << "," << m_bands[i].value_sats << "]";
    }
    json << "]";
    return json.str();
}

} // namespace enterprise
