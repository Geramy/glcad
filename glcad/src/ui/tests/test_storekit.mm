#import <XCTest/XCTest.h>

extern "C" void gc_product_stop(void);
#ifndef NDEBUG
extern "C" void gc_store_run_smoke(void);
#endif

@interface GLCADStoreKitTests : XCTestCase
@end

@implementation GLCADStoreKitTests
- (void)testLocalSubscriptionLifecycle {
#ifndef NDEBUG
    // SKTestSession needs an XCTest host; a plain app process aborts.
    NSURL* documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSString* reportPath = NSProcessInfo.processInfo.environment[@"GLCAD_STOREKIT_SMOKE_REPORT"];
    NSURL* report = reportPath ? [NSURL fileURLWithPath:reportPath] :
        [documents URLByAppendingPathComponent:@"ios-storekit-smoke.json"];
    [NSFileManager.defaultManager removeItemAtURL:report error:nil];
    gc_product_stop();
    gc_store_run_smoke();
    NSPredicate* exists = [NSPredicate predicateWithBlock:^BOOL(id object, NSDictionary* bindings) {
        (void)object; (void)bindings;
        return [NSFileManager.defaultManager fileExistsAtPath:report.path];
    }];
    XCTestExpectation* completed = [self expectationForPredicate:exists evaluatedWithObject:nil handler:nil];
    [self waitForExpectations:@[completed] timeout:140];
    NSData* data = [NSData dataWithContentsOfURL:report];
    XCTAssertNotNil(data, @"The StoreKit lifecycle did not write its result report");
    if (!data) return;
    NSError* error = nil;
    NSDictionary* result = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    XCTAssertNil(error);
    NSArray* checks = result[@"checks"];
    XCTAssertGreaterThanOrEqual(checks.count, 27u);
    for (NSDictionary* check in checks) XCTAssertTrue([check[@"passed"] boolValue], @"%@", check[@"name"]);
    XCTAssertEqual([result[@"failures"] unsignedIntegerValue], 0u);
#else
    XCTSkip(@"Local StoreKit lifecycle requires a Debug test host");
#endif
}
@end
