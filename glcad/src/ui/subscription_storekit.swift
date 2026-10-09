import Foundation
import StoreKit
#if os(iOS)
import UIKit
#endif

public typealias GcStoreCallback = @convention(c) (Int32, UnsafePointer<CChar>?,
    UnsafePointer<CChar>?, Int64, UInt32, UnsafeMutableRawPointer?) -> Void

@MainActor
private final class SubscriptionStore {
    let productID: String
    let callback: GcStoreCallback?
    let context: UnsafeMutableRawPointer?
    var product: Product?
    var listener: Task<Void, Never>?
    var expiryTask: Task<Void, Never>?
    var stopped = false
    var busy = false
    var scanVersion = 0

    init(_ productID: String, _ callback: GcStoreCallback?, _ context: UnsafeMutableRawPointer?) {
        self.productID = productID
        self.callback = callback
        self.context = context
    }
    func send(_ event: Int32, _ text: String = "", until: Int64 = 0, flags: UInt32 = 0) {
        guard !stopped else { return }
        productID.withCString { id in
            text.withCString { message in callback?(event, id, message, until, flags, context) }
        }
    }
    func start() {
        guard !productID.isEmpty else {
            send(0, "Subscriptions are not configured in this build.")
            return
        }
        // Observe before loading products so no transaction update is missed.
        listener = Task { [weak self] in
            for await result in Transaction.updates {
                guard let self, !self.stopped, !Task.isCancelled else { return }
                guard case .verified(let transaction) = result else {
                    self.send(6, "The App Store transaction could not be verified.")
                    continue
                }
                guard transaction.productID == self.productID else { continue }
                if transaction.revocationDate != nil {
                    self.send(7, "Subscription access was revoked.", flags: 1 | 2)
                }
                await self.refreshEntitlements()
                await transaction.finish()
            }
        }
        refresh()
    }
    func stop() {
        stopped = true
        listener?.cancel()
        expiryTask?.cancel()
        scanVersion += 1
    }
    func refresh() {
        guard !stopped, !productID.isEmpty, !busy else { return }
        busy = true
        send(1, "Checking App Store subscription…")
        Task {
            // Never replace signed transactions with a UserDefaults paid flag.
            await refreshEntitlements()
            do { try await loadProduct() }
            catch { send(6, "App Store unavailable: \(error.localizedDescription)") }
            busy = false
            send(9)
        }
    }
    func loadProduct() async throws {
        let products = try await Product.products(for: [productID])
        guard !stopped else { return }
        guard let found = products.first(where: { $0.id == productID }),
              found.type == .autoRenewable,
              let period = found.subscription?.subscriptionPeriod,
              period.unit == .month, period.value == 1 else {
            product = nil
            send(0, "The configured monthly subscription is unavailable in the App Store.")
            return
        }
        product = found
        send(2, "\(found.displayPrice) / month")
    }
    func refreshEntitlements() async {
        scanVersion += 1
        let version = scanVersion
        var expiration: Date?
        var grace = false
        var verificationFailed = false
        for await result in Transaction.currentEntitlements {
            guard !stopped, !Task.isCancelled else { return }
            guard case .verified(let transaction) = result else {
                verificationFailed = true
                continue
            }
            guard transaction.productID == productID,
                  transaction.productType == .autoRenewable,
                  transaction.revocationDate == nil, !transaction.isUpgraded,
                  var until = transaction.expirationDate else { continue }
            var inGrace = false
            if until <= Date(), let status = await transaction.subscriptionStatus,
               status.state == .inGracePeriod,
               case .verified(let statusTransaction) = status.transaction,
               statusTransaction.productID == productID,
               statusTransaction.originalID == transaction.originalID,
               statusTransaction.revocationDate == nil,
               case .verified(let renewal) = status.renewalInfo,
               let graceUntil = renewal.gracePeriodExpirationDate, graceUntil > Date() {
                until = graceUntil
                inGrace = true
            }
            if until > Date(), until > (expiration ?? .distantPast) {
                expiration = until
                grace = inGrace
            }
        }
        guard !stopped, version == scanVersion else { return }
        expiryTask?.cancel()
        if let expiration {
            let seconds = Int64(expiration.timeIntervalSince1970.rounded(.down))
            send(7, grace ? "Subscription is in billing grace period." : "Subscription active.",
                 until: seconds, flags: 1 | (grace ? 4 : 0))
            expiryTask = Task { [weak self] in
                let delay = max(1, min(86400, expiration.timeIntervalSinceNow + 1))
                do { try await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000)) }
                catch { return }
                guard let self, !Task.isCancelled else { return }
                await self.refreshEntitlements()
            }
        } else {
            send(7, verificationFailed ? "No verified subscription is available." : "No active subscription.")
        }
    }
    func purchase() {
        guard !stopped, !busy, let product else { return }
        busy = true
        send(3, "Waiting for the App Store…")
        Task {
            defer { busy = false }
            do {
                switch try await purchaseWithPresentationContext(product) {
                case .success(let result):
                    guard case .verified(let transaction) = result else {
                        send(6, "The purchase could not be verified. Access has not been changed.")
                        return
                    }
                    guard transaction.productID == productID else {
                        send(6, "The App Store returned a different product.")
                        return
                    }
                    await refreshEntitlements()
                    await transaction.finish()
                    send(9, "Purchase checked.")
                case .userCancelled: send(4, "Purchase cancelled.")
                case .pending: send(5, "Purchase pending approval. Access starts after approval.")
                @unknown default: send(6, "The App Store returned an unknown purchase status.")
                }
            } catch StoreKitError.userCancelled {
                send(4, "Purchase cancelled.")
            } catch { send(6, "Purchase failed: \(error.localizedDescription)") }
        }
    }
    func purchaseWithPresentationContext(_ product: Product) async throws -> Product.PurchaseResult {
        #if os(iOS)
        if #available(iOS 17.0, *) {
            let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
                .filter { $0.activationState == .foregroundActive }
            if let scene = scenes.first(where: { $0.windows.contains(where: { $0.isKeyWindow }) })
                ?? scenes.first {
                return try await product.purchase(confirmIn: scene)
            }
        }
        #endif
        return try await product.purchase()
    }
    func restore() {
        guard !stopped, !productID.isEmpty, !busy else { return }
        busy = true
        send(8, "Restoring App Store purchases…")
        Task {
            defer { busy = false }
            do {
                try await AppStore.sync()
                await refreshEntitlements()
                send(9, "Restore finished.")
            } catch { send(6, "Restore failed: \(error.localizedDescription)") }
        }
    }
}

@MainActor private var subscriptionStore: SubscriptionStore?

@_cdecl("gc_store_initialize") @MainActor
public func gcStoreInitialize(_ productID: UnsafePointer<CChar>?, _ callback: GcStoreCallback?,
                              _ context: UnsafeMutableRawPointer?) {
    subscriptionStore?.stop()
    subscriptionStore = SubscriptionStore(productID.map { String(cString: $0) } ?? "", callback, context)
    subscriptionStore?.start()
}
@_cdecl("gc_store_refresh") @MainActor
public func gcStoreRefresh() { subscriptionStore?.refresh() }
@_cdecl("gc_store_purchase") @MainActor
public func gcStorePurchase() { subscriptionStore?.purchase() }
@_cdecl("gc_store_restore") @MainActor
public func gcStoreRestore() { subscriptionStore?.restore() }
@_cdecl("gc_store_stop") @MainActor
public func gcStoreStop() { subscriptionStore?.stop(); subscriptionStore = nil }
