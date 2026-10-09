# Subscription integration and release checks

`test.glcad.monthly` in `GLCADLocal.storekit` is a local test fixture, not an
App Store Connect product. Do not configure it in a distribution build.
The production product ID must be supplied by the release configuration. An
empty ID disables purchases and cannot grant paid editing access.

## Integration

The native product screen uses `product_ui.cpp`, `iosproduct.mm` (Objective-C++
with ARC), and `ui/product.hpp`. It reads these Info.plist values:

- `GLCADPurchaseModel`: `community`, `upfront` or `subscription`. When absent,
  `GLCADPaidBuild` true means `subscription` and false means `community`. An
  unrecognized value, or `GLCADPaidBuild` true with another model, is treated
  as `subscription`.
- `GLCADPaidBuild`: boolean; true only for the subscription model.
- `GLCADSubscriptionProductID`: monthly auto-renewable subscription product.
- `GLCADSupportURL`, `GLCADPrivacyURL`, `GLCADTermsURL`: actual HTTPS pages.

An upfront build is licensed by the App Store purchase itself: editing is
always available, StoreKit is not started and the Subscription section is
hidden. It still requires the three policy URLs; `scripts/archive-ios.sh
--upfront` refuses to archive without them and builds for iPad only.

A paid build with missing configuration stays in viewer/export mode. The
Subscribe action is disabled until the product and every policy URL are
configured and StoreKit has loaded its price. A missing policy URL never
invalidates an otherwise verified existing subscription.

Call `gc_product_start` with the presenting UIViewController and a main-thread
access callback. Use `gc_product_show` for the product/support command,
`gc_product_refresh` on foreground entry, and `gc_product_stop` before releasing
the callback context. This coordinator owns the StoreKit bridge; do not call
`gc_store_initialize` separately when using it. `gc_product_edit_allowed`
rechecks the current expiration time synchronously. Bundle the application
license and third-party notices in `Resources/licenses`; the About screen
also finds the included font license files.

Build `subscription.cpp` into the common application/test sources. Build
`subscription_storekit.swift` only into the Apple store application, using
Swift 5 mode, StoreKit, and an iOS deployment target of 16 or newer. Its C ABI
is in `ui/subscription_storekit.h`; it requires no generated Swift header.

On the main thread, configure `SubscriptionState` with the product ID, then
call `gc_store_initialize`. Copy callback strings into the state through
`SubscriptionState::receive`. Refresh when returning to the foreground;
purchase and restore must follow explicit user actions. Stop the bridge
before destroying its callback context. All bridge callbacks use the main
thread. Query `can_edit` with current Unix seconds at the point of mutation,
including after a long-running operation; do not cache its result indefinitely.

StoreKit verifies signed transactions. No subscription flag is written into
the model, settings, or document. Offline restarts read StoreKit's local
signed transaction data. Access ends at the verified expiration date or
verified billing grace-period deadline. A disconnected device cannot learn
about a later refund until StoreKit receives it. Viewing, saving existing
work and exporting remain available in every subscription state.

## Xcode local test

`scripts/test-ios-simulator.sh --storekit` runs the production bridge in a
hosted XCTest session. The harness requires 27 lifecycle checks: cancellation,
purchase failure, pending approval, decline/retry/approval, bridge restart,
explicit restore and restore failure, product-loading failure with a signed
entitlement, connectivity recovery, renewal cancellation, refund, repurchase,
expiration, interrupted-purchase resolution and verified billing grace through
its actual deadline. Operation completion checks
only accept events received after that operation starts. Failure injection
requires iOS 17 or macOS 14; production deployment still supports iOS 16.

Simulating product-loading network failure tests the bridge's fallback to
signed entitlements; it does not prove that every StoreKit service works with
the physical device offline. Fully offline operation, real sandbox
configuration and device lifecycle remain separate checks below.

All 27 hosted checks passed on iOS 26.1 (23B80), using an iPad (A16) simulator
and Xcode 26.6. Logs, results and source hashes are in
`build/release-evidence/storekit-ios26.1/`. CI explicitly installs this runtime
for StoreKit while retaining the separate native CAD smoke on its default
simulator. The local iOS 26.5 runtime fails before product loading; it is not
reported as passing. StoreKit test overrides are reset before the renewal
sequence, retaining transaction history, because clearing individual simulated
errors left cancellation state in the local test service during validation.

1. Configure a separate paid development build with product `test.glcad.monthly`
   and configured policy/support URLs. These fixture settings must not enter
   the production configuration.
2. In its Run scheme, select `GLCADLocal.storekit` as StoreKit Configuration.
3. Launch on the simulator and open the subscription UI. Confirm the displayed
   localized price comes from StoreKit and editing initially requires access.
4. Use Debug > StoreKit > Manage Transactions and the configuration's testing
   settings to exercise the cases below.

| Case | Required result |
|---|---|
| Buy, cancel confirmation | No charge or entitlement; editing remains locked |
| Buy, approve | Verified entitlement unlocks editing |
| Ask to Buy / pending | No access until approval arrives through updates |
| Decline pending purchase | No access; user can retry after refreshing |
| Finish/restore after relaunch | Existing verified access returns, no duplicate purchase required |
| Cancel renewal | Current paid period remains usable through expiration |
| Expire subscription | Editing stops; document remains viewable/saveable/exportable |
| Refund/revoke | Update removes paid access without deleting work |
| Renew after expiration | Verified update restores editing |
| Fail renewal with grace enabled | Access lasts until the verified grace deadline |
| Offline while subscribed | Signed local access lasts until its deadline |
| Offline without entitlement | No default unlock; viewing/export still work |
| Invalid/unavailable product | Purchase disabled, explanatory message, no fabricated price |
| Unverified transaction | No unlock and no successful-purchase message |

Run the same purchase/restore/renewal/refund flows in Apple's sandbox with the
real configured product before distribution. The C++ state tests and Swift
typechecks do not prove storefront configuration, transaction delivery, or
real-device lifecycle behavior.

## References

- [Current entitlements](https://developer.apple.com/documentation/storekit/transaction/currententitlements)
- [Purchase results](https://developer.apple.com/documentation/storekit/product/purchaseresult)
- [Restore with AppStore.sync](https://developer.apple.com/documentation/storekit/appstore/sync())
- [Billing grace-period deadline](https://developer.apple.com/documentation/storekit/product/subscriptioninfo/renewalinfo/graceperiodexpirationdate)
- [Testing purchases in Xcode](https://developer.apple.com/documentation/storekit/testing-in-app-purchases-in-xcode)
