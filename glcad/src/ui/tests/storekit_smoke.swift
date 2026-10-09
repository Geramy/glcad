#if DEBUG && (targetEnvironment(simulator) || os(macOS))
import Foundation
import StoreKit
import StoreKitTest

@MainActor private final class StoreProbe {
    struct Event { var kind: Int32; var until: Int64; var flags: UInt32; var message: String }
    var events: [Event] = []
    var checks: [[String: Any]] = []
    func check(_ name: String, _ passed: Bool) {
        checks.append(["name": name, "passed": passed])
        NSLog("StoreKit smoke: %@ %@", passed ? "PASS" : "FAIL", name)
    }
    func require(_ name: String, seconds: Int = 15, _ predicate: @MainActor () -> Bool) async throws {
        let passed = await wait(seconds: seconds, predicate)
        check(name, passed)
        guard passed else { throw NSError(domain: "StoreKitSmoke", code: 1,
            userInfo: [NSLocalizedDescriptionKey: name]) }
    }
    func received(_ kind: Int32, since index: Int) -> Bool {
        events.dropFirst(index).contains { $0.kind == kind }
    }
    func wait(seconds: Int = 15, _ predicate: @MainActor () -> Bool) async -> Bool {
        for _ in 0..<(seconds * 10) {
            if predicate() { return true }
            try? await Task.sleep(nanoseconds: 100_000_000)
        }
        return predicate()
    }
    var active: Bool {
        guard let e = events.last(where: { $0.kind == 7 }) else { return false }
        return e.flags & 1 != 0 && e.flags & 2 == 0 && e.until > Int64(Date().timeIntervalSince1970)
    }
}

private func storeSmokeEvent(_ kind: Int32, _ product: UnsafePointer<CChar>?,
                            _ message: UnsafePointer<CChar>?, _ until: Int64,
                            _ flags: UInt32, _ context: UnsafeMutableRawPointer?) {
    guard let context else { return }
    let probe = Unmanaged<StoreProbe>.fromOpaque(context).takeUnretainedValue()
    let text = message.map { String(cString: $0) } ?? ""
    Task { @MainActor in probe.events.append(.init(kind: kind, until: until, flags: flags, message: text)) }
}

@_cdecl("gc_store_run_smoke") @MainActor
public func gcStoreRunSmoke() {
    Task { @MainActor in
        let probe = StoreProbe()
        let context = Unmanaged.passUnretained(probe).toOpaque()
        let product = "test.glcad.monthly"
        var session: SKTestSession?
        do {
            let test = try SKTestSession(configurationFileNamed: "GLCADLocal")
            session = test
            test.resetToDefaultState(); test.clearTransactions(); test.disableDialogs = true
            product.withCString { gcStoreInitialize($0, storeSmokeEvent, context) }
            try await probe.require("local monthly price loads through production bridge") {
                probe.received(2, since: 0) && probe.received(9, since: 0)
            }
            probe.check("new customer has no entitlement", probe.received(7, since: 0) && !probe.active)
            guard #available(iOS 17.0, macOS 14.0, *) else {
                throw NSError(domain: "StoreKitSmoke", code: 3, userInfo:
                    [NSLocalizedDescriptionKey: "Full failure-injection coverage requires iOS 17 or macOS 14."])
            }

            var mark = probe.events.count
            gcStorePurchase()
            try await probe.require("new customer purchase completes with verified access") {
                probe.active && probe.received(9, since: mark)
            }
            try test.expireSubscription(productIdentifier: product)
            try await probe.require("fresh purchase expiration removes access") { !probe.active }
            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("fresh expired subscription can be purchased again") {
                probe.active && probe.received(9, since: mark)
            }
            guard let firstPurchase = test.allTransactions().last else {
                throw NSError(domain: "StoreKitSmoke", code: 7)
            }
            try test.refundTransaction(identifier: firstPurchase.identifier)
            try await probe.require("initial purchase refund returns to viewer access") { !probe.active }

            mark = probe.events.count
            try await test.setSimulatedError(.generic(.userCancelled), forAPI: .purchase)
            gcStorePurchase()
            try await probe.require("cancelled purchase reports cancellation without access") {
                probe.received(4, since: mark) && !probe.active
            }
            try await test.setSimulatedError(.generic(.notAvailableInStorefront), forAPI: .purchase)
            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("failed purchase reports an error without access") {
                probe.received(6, since: mark) && !probe.active
            }
            try await test.setSimulatedError(nil, forAPI: .purchase)

            test.askToBuyEnabled = true
            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("pending approval does not grant access") {
                probe.received(5, since: mark) && !probe.active
            }
            guard let declined = test.allTransactions().last else {
                throw NSError(domain: "StoreKitSmoke", code: 4)
            }
            try test.declineAskToBuyTransaction(identifier: declined.identifier)
            mark = probe.events.count
            gcStoreRefresh()
            try await probe.require("declined approval leaves the customer without access") {
                probe.received(9, since: mark) && !probe.active
            }
            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("customer can retry after declined approval") {
                probe.received(5, since: mark) && !probe.active
            }
            guard let approved = test.allTransactions().last else {
                throw NSError(domain: "StoreKitSmoke", code: 5)
            }
            try test.approveAskToBuyTransaction(identifier: approved.identifier)
            try await probe.require("approval update delivers verified access") { probe.active }
            test.askToBuyEnabled = false

            gcStoreStop(); probe.events.removeAll()
            product.withCString { gcStoreInitialize($0, storeSmokeEvent, context) }
            try await probe.require("bridge restart restores signed local entitlement") {
                probe.active && probe.received(9, since: 0)
            }
            mark = probe.events.count
            gcStoreRestore()
            try await probe.require("explicit restore completes and retains verified access") {
                probe.active && probe.received(8, since: mark) && probe.received(9, since: mark)
            }
            try await test.setSimulatedError(.generic(.networkError(URLError(.notConnectedToInternet))),
                                             forAPI: .appStoreSync)
            mark = probe.events.count
            gcStoreRestore()
            try await probe.require("failed restore preserves existing signed access") {
                probe.received(6, since: mark) && probe.active
            }
            try await test.setSimulatedError(nil, forAPI: .appStoreSync)
            try await test.setSimulatedError(.generic(.networkError(URLError(.notConnectedToInternet))),
                                             forAPI: .loadProducts)
            gcStoreStop(); probe.events.removeAll()
            product.withCString { gcStoreInitialize($0, storeSmokeEvent, context) }
            try await probe.require("product loading failure preserves signed access on restart") {
                probe.received(6, since: 0) && probe.received(9, since: 0) && probe.active
            }
            try await test.setSimulatedError(nil, forAPI: .loadProducts)
            mark = probe.events.count
            gcStoreRefresh()
            try await probe.require("product loading recovers after connectivity returns") {
                probe.received(2, since: mark) && probe.received(9, since: mark) && probe.active
            }
            if let transaction = test.allTransactions().last {
                try test.disableAutoRenewForTransaction(identifier: transaction.identifier)
                mark = probe.events.count
                gcStoreRefresh()
                try await probe.require("cancelling renewal retains paid period") {
                    probe.received(9, since: mark) && probe.active
                }
                try test.refundTransaction(identifier: transaction.identifier)
                try await probe.require("refund update removes entitlement") { !probe.active }
            } else { probe.check("local transaction exists", false) }
            // Clearing errors alone can leave cancellation state in the local test service.
            test.resetToDefaultState()
            test.disableDialogs = true
            try test.buyProduct(productIdentifier: product)
            try await probe.require("transaction update restores access after repurchase") { probe.active }
            try test.expireSubscription(productIdentifier: product)
            try await probe.require("explicit expiration removes editing entitlement") { !probe.active }

            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("purchase button grants verified access after expiration") {
                probe.active && probe.received(9, since: mark)
            }
            try test.expireSubscription(productIdentifier: product)
            try await probe.require("second expiration removes editing entitlement") { !probe.active }

            test.interruptedPurchasesEnabled = true
            mark = probe.events.count
            gcStorePurchase()
            try await probe.require("interrupted purchase does not grant access") {
                (probe.received(6, since: mark) || probe.received(5, since: mark)) && !probe.active
            }
            guard let interrupted = test.allTransactions().last else {
                throw NSError(domain: "StoreKitSmoke", code: 6)
            }
            test.interruptedPurchasesEnabled = false
            try test.resolveIssueForTransaction(identifier: interrupted.identifier)
            try await probe.require("resolved interrupted purchase restores access through updates") { probe.active }

            test.shouldEnterBillingRetryOnRenewal = true
            test.billingGracePeriodIsEnabled = true
            try test.forceRenewalOfSubscription(productIdentifier: product)
            gcStoreRefresh()
            try await probe.require("verified billing grace retains access until its deadline") {
                probe.active && (probe.events.last(where: { $0.kind == 7 })?.flags ?? 0) & 4 != 0
            }
            try await probe.require("billing grace deadline removes editing entitlement", seconds: 60) {
                guard let event = probe.events.last(where: { $0.kind == 7 }) else { return false }
                return event.flags & 1 == 0 && !probe.active
            }
        } catch {
            probe.check("StoreKit lifecycle completed: \(error.localizedDescription)", false)
        }
        gcStoreStop()
        session?.resetToDefaultState()
        session?.clearTransactions()
        let report: [String: Any] = ["checks": probe.checks,
            "failures": probe.checks.filter { ($0["passed"] as? Bool) != true }.count,
            "environment": "Xcode local StoreKit test; no production purchase"]
        do {
            let url: URL
            if let path = ProcessInfo.processInfo.environment["GLCAD_STOREKIT_SMOKE_REPORT"] {
                url = URL(fileURLWithPath: path)
            } else {
                url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                    .appendingPathComponent("ios-storekit-smoke.json")
            }
            try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
                .write(to: url, options: .atomic)
        } catch { NSLog("StoreKit smoke report failed: %@", error.localizedDescription) }
    }
}
#endif
