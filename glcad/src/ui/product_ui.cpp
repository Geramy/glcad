#include "ui/product.hpp"
#include <algorithm>
#include <cctype>

namespace gcapp {
bool product_https_url(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    const auto end = url.find_first_of("/?#", 8);
    const auto authority = url.substr(8, end == std::string::npos ? end : end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos) return false;
    return std::none_of(url.begin(), url.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f || c == '\\';
    });
}
PurchaseModel purchase_model_from_info(const std::string& model, bool paid_build) {
    if (model.empty()) return paid_build ? PurchaseModel::Subscription : PurchaseModel::Community;
    if (model == "community" && !paid_build) return PurchaseModel::Community;
    if (model == "upfront" && !paid_build) return PurchaseModel::Upfront;
    return PurchaseModel::Subscription;
}
std::string ProductConfiguration::configuration_problem() const {
    if (model == PurchaseModel::Community) return {};
    if (subscription_build() && subscription_product_id.empty()) return "A subscription product has not been configured for this build.";
    if (!product_https_url(support_url)) return "A support URL has not been configured for this build.";
    if (!product_https_url(privacy_url)) return "A privacy policy URL has not been configured for this build.";
    if (!product_https_url(terms_url)) return "A terms of use URL has not been configured for this build.";
    return {};
}
bool ProductConfiguration::purchase_configured() const {
    return subscription_build() && configuration_problem().empty();
}
bool product_edit_allowed(const ProductConfiguration& c, const SubscriptionState& s, std::int64_t now) {
    return !c.subscription_build() || (!c.subscription_product_id.empty() &&
        c.subscription_product_id == s.product_id() && s.can_edit(now));
}
std::string product_access_description(const ProductConfiguration& c, const SubscriptionState& s,
                                       std::int64_t now) {
    if (c.model == PurchaseModel::Upfront) return "App Store edition — licensed for commercial use with this purchase.";
    if (!c.subscription_build()) return "Source build — free for noncommercial use. Commercial use requires a paid license or written agreement.";
    switch (s.access(now)) {
    case SubscriptionAccess::Active: return "Subscription active — editing is available.";
    case SubscriptionAccess::GracePeriod: return "Billing grace period — editing remains available while Apple resolves payment.";
    case SubscriptionAccess::Expired: return "Subscription expired — viewing, saving existing work and exporting remain available.";
    case SubscriptionAccess::Revoked: return "Subscription access was revoked — your files remain viewable, saveable and exportable.";
    case SubscriptionAccess::Checking: return "Checking subscription access…";
    case SubscriptionAccess::Disabled: return "Purchases are unavailable in this build. Viewing and exporting remain available.";
    case SubscriptionAccess::None: return "Subscribe to create and edit. Viewing, saving existing work and exporting remain available.";
    }
    return {};
}
}
