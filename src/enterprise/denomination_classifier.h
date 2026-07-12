#ifndef BITCOIN_ENTERPRISE_DENOMINATION_CLASSIFIER_H
#define BITCOIN_ENTERPRISE_DENOMINATION_CLASSIFIER_H

#include <consensus/amount.h>

#include <optional>
#include <string>

namespace enterprise {

inline constexpr const char* DENOMINATION_CLASSIFIER_VERSION{"denomination-v2"};

enum class DenominationCategory {
    USD,
    SATS,
    UNKNOWN,
    AMBIGUOUS,
};

struct DenominationPriceWindow {
    double price{0.0};
    double low{0.0};
    double high{0.0};
    std::string source;

    [[nodiscard]] bool Valid() const;
};

struct DenominationResult {
    DenominationCategory category{DenominationCategory::UNKNOWN};
    double usd_score{0.0};
    double sats_score{0.0};
    double confidence{0.0};
};

[[nodiscard]] DenominationResult ClassifyOutputDenomination(
    CAmount nvalue_sats,
    const std::optional<DenominationPriceWindow>& price_window);

} // namespace enterprise

#endif // BITCOIN_ENTERPRISE_DENOMINATION_CLASSIFIER_H
