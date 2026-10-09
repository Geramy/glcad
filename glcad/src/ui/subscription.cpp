#include "ui/subscription.hpp"
#include <utility>

namespace gcapp {
void SubscriptionState::configure(std::string product_id) {
    *this = SubscriptionState{};
    product_id_ = std::move(product_id);
    if (configured()) request_ = SubscriptionRequest::Checking;
    else message_ = "Subscriptions are not configured in this build.";
}

void SubscriptionState::receive(int event, const char* product_id, const char* message,
                                std::int64_t until, std::uint32_t flags) {
    if (!configured() || !product_id || product_id_ != product_id) return;
    switch (event) {
    case GC_STORE_CHECKING: request_ = SubscriptionRequest::Checking; break;
    case GC_STORE_PRODUCT: price_ = message ? message : ""; return;
    case GC_STORE_PURCHASING: request_ = SubscriptionRequest::Purchasing; break;
    case GC_STORE_RESTORING: request_ = SubscriptionRequest::Restoring; break;
    case GC_STORE_PENDING: request_ = SubscriptionRequest::Pending; break;
    case GC_STORE_CANCELLED: request_ = SubscriptionRequest::Idle; break;
    case GC_STORE_COMPLETE:
        request_ = SubscriptionRequest::Idle;
        if (!message || !*message) return;
        break;
    case GC_STORE_ERROR:
        request_ = SubscriptionRequest::Idle;
        checked_ = true;
        break;
    case GC_STORE_ENTITLEMENT:
        checked_ = true;
        verified_ = (flags & GC_STORE_VERIFIED) != 0;
        // Revocation is accepted only from a verified transaction.
        if (verified_ && (flags & GC_STORE_REVOKED)) revoked_ = true;
        else if (verified_ && until > 0) revoked_ = false;
        valid_until_ = verified_ && !revoked_ ? until : 0;
        grace_ = verified_ && (flags & GC_STORE_GRACE_PERIOD);
        previously_entitled_ |= verified_ && until > 0;
        if (request_ == SubscriptionRequest::Checking ||
            (request_ == SubscriptionRequest::Pending && valid_until_ > 0))
            request_ = SubscriptionRequest::Idle;
        break;
    case GC_STORE_DISABLED:
        price_.clear(); request_ = SubscriptionRequest::Idle; break;
    default: return;
    }
    message_ = message ? message : "";
}

SubscriptionAccess SubscriptionState::access(std::int64_t now) const {
    if (!configured()) return SubscriptionAccess::Disabled;
    if (revoked_) return SubscriptionAccess::Revoked;
    if (verified_ && valid_until_ > now)
        return grace_ ? SubscriptionAccess::GracePeriod : SubscriptionAccess::Active;
    if (previously_entitled_) return SubscriptionAccess::Expired;
    return checked_ ? SubscriptionAccess::None : SubscriptionAccess::Checking;
}
bool SubscriptionState::can_edit(std::int64_t now) const {
    const auto a = access(now);
    return a == SubscriptionAccess::Active || a == SubscriptionAccess::GracePeriod;
}
bool SubscriptionState::can_purchase() const {
    return configured() && !price_.empty() && request_ == SubscriptionRequest::Idle;
}
}
