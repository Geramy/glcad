#include "test.hpp"
#include "ui/subscription.hpp"
#include "ui/product.hpp"

using namespace gcapp;
namespace {
constexpr auto product = "test.glcad.monthly";
constexpr std::int64_t now = 2000000000;
SubscriptionState configured() {
    SubscriptionState state;
    state.configure(product);
    state.receive(GC_STORE_ENTITLEMENT, product, "", 0, 0);
    state.receive(GC_STORE_PRODUCT, product, "$5.00 / month", 0, 0);
    state.receive(GC_STORE_COMPLETE, product, "", 0, 0);
    return state;
}
void grant(SubscriptionState& s, std::int64_t until = now + 100) {
    s.receive(GC_STORE_ENTITLEMENT, product, "Active", until, GC_STORE_VERIFIED);
}
void files_remain_accessible(const SubscriptionState& s) {
    CHECK(s.can_view()); CHECK(s.can_export()); CHECK(s.can_save_existing());
}
}

TEST(subscription_unconfigured_and_unverified_never_unlock_editing) {
    SubscriptionState s;
    CHECK(s.access(now) == SubscriptionAccess::Disabled);
    CHECK(!s.can_edit(now)); CHECK(!s.can_purchase());
    grant(s);
    CHECK(!s.can_edit(now));
    s.configure(product);
    s.receive(GC_STORE_ENTITLEMENT, product, "Unverified", now + 100, 0);
    CHECK(!s.can_edit(now));
    s.receive(GC_STORE_ENTITLEMENT, "another.product", "", now + 100, GC_STORE_VERIFIED);
    CHECK(!s.can_edit(now));
    files_remain_accessible(s);
}
TEST(subscription_purchase_cancel_pending_and_verified_approval) {
    auto s = configured();
    CHECK(s.can_purchase());
    s.receive(GC_STORE_PURCHASING, product, "", 0, 0);
    CHECK(!s.can_purchase()); CHECK(!s.can_edit(now));
    s.receive(GC_STORE_CANCELLED, product, "Cancelled", 0, 0);
    CHECK(s.can_purchase()); CHECK(!s.can_edit(now));
    s.receive(GC_STORE_PENDING, product, "Pending", 0, 0);
    CHECK(s.request() == SubscriptionRequest::Pending);
    CHECK(!s.can_purchase()); CHECK(!s.can_edit(now));
    grant(s);
    CHECK(s.can_edit(now));
    CHECK(s.request() == SubscriptionRequest::Idle);
}
TEST(subscription_offline_keeps_verified_access_only_until_expiry) {
    auto s = configured(); grant(s);
    s.receive(GC_STORE_CHECKING, product, "", 0, 0);
    s.receive(GC_STORE_ERROR, product, "Offline", 0, 0);
    CHECK(s.can_edit(now + 99));
    CHECK(!s.can_edit(now + 100));
    CHECK(s.access(now + 100) == SubscriptionAccess::Expired);
    s.receive(GC_STORE_COMPLETE, product, "", 0, 0);
    CHECK(s.message() == "Offline");
    files_remain_accessible(s);
}
TEST(subscription_restore_does_not_grant_without_verified_entitlement) {
    auto s = configured();
    s.receive(GC_STORE_RESTORING, product, "", 0, 0);
    CHECK(!s.can_purchase());
    s.receive(GC_STORE_COMPLETE, product, "Restore finished", 0, 0);
    CHECK(!s.can_edit(now));
    s.receive(GC_STORE_RESTORING, product, "", 0, 0);
    grant(s);
    s.receive(GC_STORE_COMPLETE, product, "Restore finished", 0, 0);
    CHECK(s.can_edit(now));
    s.configure(product);
    CHECK(!s.can_edit(now));
    grant(s);
    CHECK(s.can_edit(now));
}
TEST(subscription_refund_removes_access_and_renewal_restores_it) {
    auto s = configured(); grant(s);
    s.receive(GC_STORE_ENTITLEMENT, product, "Revoked", 0,
              GC_STORE_VERIFIED | GC_STORE_REVOKED);
    CHECK(s.access(now) == SubscriptionAccess::Revoked);
    CHECK(!s.can_edit(now));
    s.receive(GC_STORE_ENTITLEMENT, product, "None", 0, 0);
    CHECK(s.access(now) == SubscriptionAccess::Revoked);
    files_remain_accessible(s);
    grant(s, now + 200);
    CHECK(s.can_edit(now + 199));
    CHECK(!s.can_edit(now + 200));
}
TEST(subscription_grace_period_requires_verified_bounded_authority) {
    auto s = configured();
    s.receive(GC_STORE_ENTITLEMENT, product, "Grace", now + 30, GC_STORE_GRACE_PERIOD);
    CHECK(!s.can_edit(now));
    s.receive(GC_STORE_ENTITLEMENT, product, "Grace", now + 30,
              GC_STORE_VERIFIED | GC_STORE_GRACE_PERIOD);
    CHECK(s.access(now) == SubscriptionAccess::GracePeriod);
    CHECK(s.can_edit(now + 29)); CHECK(!s.can_edit(now + 30));
    files_remain_accessible(s);
}
TEST(subscription_missing_product_disables_purchase_without_clearing_paid_access) {
    auto s = configured(); grant(s);
    s.receive(GC_STORE_DISABLED, product, "Product unavailable", 0, 0);
    CHECK(!s.can_purchase()); CHECK(s.can_edit(now));
    s.configure("");
    CHECK(s.access(now) == SubscriptionAccess::Disabled);
    CHECK(!s.can_purchase()); CHECK(!s.can_edit(now));
    files_remain_accessible(s);
}

TEST(subscription_product_configuration_never_treats_incomplete_paid_build_as_community) {
    ProductConfiguration c;
    auto s = configured();
    CHECK(product_edit_allowed(c, s, now));
    CHECK(!c.purchase_configured());
    c.model = PurchaseModel::Subscription;
    CHECK(!product_edit_allowed(c, s, now));
    CHECK(!c.purchase_configured());
    CHECK(!c.configuration_problem().empty());
    c.subscription_product_id = product;
    c.support_url = "https://example.invalid/support";
    c.privacy_url = "https://example.invalid/privacy";
    CHECK(!c.purchase_configured());
    c.terms_url = "https://example.invalid/terms";
    CHECK(c.purchase_configured());
    CHECK(!product_edit_allowed(c, s, now));
    grant(s);
    CHECK(product_edit_allowed(c, s, now));
    CHECK(!product_edit_allowed(c, s, now + 100));
    c.privacy_url.clear();
    CHECK(!c.purchase_configured());
    CHECK(product_edit_allowed(c, s, now));
    c.subscription_product_id = "another.product";
    CHECK(!product_edit_allowed(c, s, now));
}

TEST(subscription_upfront_purchase_allows_editing_without_a_subscription) {
    ProductConfiguration c;
    c.model = PurchaseModel::Upfront;
    SubscriptionState s;
    s.configure("");
    CHECK(product_edit_allowed(c, s, now));
    CHECK(!c.purchase_configured());
    CHECK(!c.configuration_problem().empty());
    c.support_url = "https://example.invalid/support";
    c.privacy_url = "https://example.invalid/privacy";
    c.terms_url = "https://example.invalid/terms";
    CHECK(c.configuration_problem().empty());
    CHECK(!c.purchase_configured());
    CHECK(product_edit_allowed(c, s, now + 100000));
    const auto text = product_access_description(c, s, now);
    CHECK(text.find("App Store edition") != std::string::npos);
    CHECK(text.find("Source build") == std::string::npos);
    c.terms_url = "http://example.invalid/terms";
    CHECK(!c.configuration_problem().empty());
    CHECK(product_edit_allowed(c, s, now));
}

TEST(subscription_purchase_model_from_info_fails_closed) {
    CHECK(purchase_model_from_info("", false) == PurchaseModel::Community);
    CHECK(purchase_model_from_info("", true) == PurchaseModel::Subscription);
    CHECK(purchase_model_from_info("community", false) == PurchaseModel::Community);
    CHECK(purchase_model_from_info("upfront", false) == PurchaseModel::Upfront);
    CHECK(purchase_model_from_info("subscription", false) == PurchaseModel::Subscription);
    CHECK(purchase_model_from_info("upfront", true) == PurchaseModel::Subscription);
    CHECK(purchase_model_from_info("community", true) == PurchaseModel::Subscription);
    CHECK(purchase_model_from_info("Upfront", false) == PurchaseModel::Subscription);
}

TEST(subscription_product_links_reject_empty_unsafe_and_malformed_urls) {
    CHECK(product_https_url("https://example.invalid/support?source=app"));
    CHECK(!product_https_url(""));
    CHECK(!product_https_url("http://example.invalid"));
    CHECK(!product_https_url("javascript:alert(1)"));
    CHECK(!product_https_url("https://"));
    CHECK(!product_https_url("https:///privacy"));
    CHECK(!product_https_url("https://user@example.invalid/privacy"));
    CHECK(!product_https_url("https://example.invalid/\nprivacy"));
    CHECK(!product_https_url("https://example.invalid\\privacy"));
}
