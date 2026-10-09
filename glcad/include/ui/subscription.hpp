#pragma once
#include <cstdint>
#include <string>
#include "ui/subscription_storekit.h"

namespace gcapp {

enum class SubscriptionAccess { Disabled, Checking, None, Active, GracePeriod, Expired, Revoked };
enum class SubscriptionRequest { Idle, Checking, Purchasing, Pending, Restoring };

class SubscriptionState {
public:
    void configure(std::string product_id);
    void receive(int event, const char* product_id, const char* message,
                 std::int64_t valid_until_unix, std::uint32_t flags);
    SubscriptionAccess access(std::int64_t now_unix) const;
    bool can_edit(std::int64_t now_unix) const;
    bool can_view() const { return true; }
    bool can_export() const { return true; }
    bool can_save_existing() const { return true; }
    bool can_purchase() const;
    bool configured() const { return !product_id_.empty(); }
    SubscriptionRequest request() const { return request_; }
    const std::string& product_id() const { return product_id_; }
    const std::string& price() const { return price_; }
    const std::string& message() const { return message_; }
    std::int64_t valid_until() const { return valid_until_; }
private:
    std::string product_id_, price_, message_;
    SubscriptionRequest request_ = SubscriptionRequest::Idle;
    std::int64_t valid_until_ = 0;
    bool checked_ = false, verified_ = false, revoked_ = false, grace_ = false;
    bool previously_entitled_ = false;
};

}
