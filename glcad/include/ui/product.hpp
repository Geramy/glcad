#pragma once
#include <string>
#include "ui/subscription.hpp"

namespace gcapp {
enum class PurchaseModel { Community, Upfront, Subscription };
PurchaseModel purchase_model_from_info(const std::string& model, bool paid_build);
struct ProductConfiguration {
    PurchaseModel model = PurchaseModel::Community;
    std::string subscription_product_id, support_url, privacy_url, terms_url;
    bool subscription_build() const { return model == PurchaseModel::Subscription; }
    bool purchase_configured() const;
    std::string configuration_problem() const;
};
bool product_https_url(const std::string& url);
bool product_edit_allowed(const ProductConfiguration&, const SubscriptionState&,
                          std::int64_t now_unix);
std::string product_access_description(const ProductConfiguration&, const SubscriptionState&,
                                       std::int64_t now_unix);
}

using GcProductAccessCallback = void (*)(int edit_allowed, int subscription_build, void* context);
using GcProductSampleCallback = void (*)(const char* bundled_sample_path, void* context);
extern "C" {
void gc_product_start(void* presenting_view_controller, GcProductAccessCallback, void* context);
void gc_product_show(void);
void gc_product_refresh(void);
void gc_product_stop(void);
int gc_product_edit_allowed(void);
void gc_product_set_sample_callback(GcProductSampleCallback, void* context);
}
