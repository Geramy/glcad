#import <UIKit/UIKit.h>
#include "ui/product.hpp"
#include <chrono>

namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string info_string(NSString* key) {
    id value = [[NSBundle mainBundle] objectForInfoDictionaryKey:key];
    return [value isKindOfClass:NSString.class] ? [(NSString*)value UTF8String] : "";
}
}

@class GCProductController;
@interface GCProductCoordinator : NSObject {
@public
    gcapp::ProductConfiguration configuration;
    gcapp::SubscriptionState subscription;
    GcProductAccessCallback accessCallback;
    void* accessContext;
    int lastAccess;
    GcProductSampleCallback sampleCallback;
    void* sampleContext;
    bool welcomePending;
}
@property(nonatomic, weak) UIViewController* presenter;
@property(nonatomic, weak) GCProductController* screen;
@property(nonatomic, strong) NSTimer* expirationTimer;
- (void)notifyAccess;
- (void)show;
- (void)showWelcomeIfNeeded;
@end

@interface GCProductController : UITableViewController <UIAdaptivePresentationControllerDelegate>
@property(nonatomic, weak) GCProductCoordinator* owner;
@end

@implementation GCProductCoordinator
- (void)notifyAccess {
    const int allowed = gcapp::product_edit_allowed(configuration, subscription, unix_now());
    if (lastAccess != allowed) {
        lastAccess = allowed;
        if (accessCallback) accessCallback(allowed, configuration.subscription_build() ? 1 : 0, accessContext);
        [self.screen.tableView reloadData];
    }
}
- (void)show {
    if (self.screen) return;
    UIViewController* presenter = self.presenter;
    while (presenter.presentedViewController) presenter = presenter.presentedViewController;
    if (!presenter || presenter.isBeingDismissed) return;
    GCProductController* screen = [[GCProductController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    screen.owner = self;
    self.screen = screen;
    UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:screen];
    nav.modalPresentationStyle = UIModalPresentationFormSheet;
    nav.presentationController.delegate = screen;
    [presenter presentViewController:nav animated:YES completion:nil];
}
- (void)showWelcomeIfNeeded {
    if (!welcomePending || !self.presenter.view.window || self.presenter.presentedViewController) return;
    welcomePending = false;
    [self show];
}
@end

@implementation GCProductController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"GLCAD";
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
    self.tableView.rowHeight = UITableViewAutomaticDimension;
    self.tableView.estimatedRowHeight = 64;
    UIView* header = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 500, 260)];
    UIStackView* stack = [[UIStackView alloc] init];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    NSArray<NSString*>* steps = @[@"Start with a simple plate",
        @"1. Open the sample or sketch a closed shape. Tap an area to select it.",
        @"2. Extrude and enter a size. Preview the change, then tap OK to commit it.",
        @"3. Select a face to adjust it. Use Undo freely, then save or export your work."];
    for (NSUInteger i = 0; i < steps.count; ++i) {
        UILabel* label = [[UILabel alloc] init];
        label.text = steps[i]; label.numberOfLines = 0;
        label.font = [UIFont preferredFontForTextStyle:i == 0 ? UIFontTextStyleTitle2 : UIFontTextStyleBody];
        label.adjustsFontForContentSizeCategory = YES;
        [stack addArrangedSubview:label];
    }
    UIButton* sample = [UIButton buttonWithType:UIButtonTypeSystem];
    sample.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [sample setTitle:@"Open sample plate" forState:UIControlStateNormal];
    [sample addTarget:self action:@selector(openSample) forControlEvents:UIControlEventTouchUpInside];
    sample.accessibilityHint = @"Opens a new drawing from the bundled millimetre sample.";
    [stack addArrangedSubview:sample];
    [header addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:header.topAnchor constant:20],
        [stack.bottomAnchor constraintEqualToAnchor:header.bottomAnchor constant:-12],
        [stack.leadingAnchor constraintEqualToAnchor:header.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:header.trailingAnchor constant:-20]]];
    self.tableView.tableHeaderView = header;
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    UIView* header = self.tableView.tableHeaderView;
    const CGFloat width = self.tableView.bounds.size.width;
    const CGSize size = [header systemLayoutSizeFittingSize:CGSizeMake(width, UILayoutFittingCompressedSize.height)
        withHorizontalFittingPriority:UILayoutPriorityRequired verticalFittingPriority:UILayoutPriorityFittingSizeLevel];
    if (fabs(header.frame.size.height - size.height) > 0.5 || header.frame.size.width != width) {
        header.frame = CGRectMake(0, 0, width, size.height);
        self.tableView.tableHeaderView = header;
    }
}
- (void)rememberWelcomeDismissed {
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"GLCADWelcomeDismissed.v1"];
    if (self.owner) self.owner->welcomePending = false;
}
- (void)presentationControllerDidDismiss:(UIPresentationController*)presentationController {
    [self rememberWelcomeDismissed];
}
- (void)close {
    [self rememberWelcomeDismissed];
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)openSample {
    auto* o = self.owner;
    NSURL* sample = [NSBundle.mainBundle URLForResource:@"GettingStarted" withExtension:@"dxf" subdirectory:@"samples"];
    if (!sample) sample = [NSBundle.mainBundle URLForResource:@"GettingStarted" withExtension:@"dxf"];
    if (!o || !o->sampleCallback || !sample) {
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Sample unavailable"
            message:@"The sample drawing has not been included or connected in this development build."
            preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        return;
    }
    [self rememberWelcomeDismissed];
    const auto callback = o->sampleCallback;
    void* context = o->sampleContext;
    NSString* path = sample.path;
    __weak GCProductCoordinator* weakOwner = o;
    [self dismissViewControllerAnimated:YES completion:^{
        if (weakOwner) callback(path.UTF8String, context);
    }];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView { return 4; }
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section {
    if (section == 1 && !(self.owner && self.owner->configuration.subscription_build())) return 0;
    switch (section) { case 0: return 3; case 1: return 3; case 2: return 3; default: return 2; }
}
- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section {
    if (section == 1 && !(self.owner && self.owner->configuration.subscription_build())) return nil;
    return @[@"Access", @"Subscription", @"Help and policies", @"About"][section];
}
- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section {
    auto* owner = self.owner;
    if (section == 1 && owner && owner->configuration.subscription_build())
        return @"Payment is charged to your Apple Account after confirmation. The monthly subscription automatically renews unless cancelled at least 24 hours before the current period ends. Manage or cancel it in your App Store account. Existing files remain available to view, save and export after access ends.";
    return nil;
}
- (BOOL)actionEnabled:(NSIndexPath*)path {
    auto* o = self.owner;
    if (!o) return NO;
    if (path.section == 1) {
        if (path.row == 0) return o->configuration.purchase_configured() &&
            o->subscription.can_purchase() && !o->subscription.can_edit(unix_now());
        if (path.row == 1) return o->configuration.subscription_build() && o->subscription.configured() &&
            o->subscription.request() != gcapp::SubscriptionRequest::Purchasing &&
            o->subscription.request() != gcapp::SubscriptionRequest::Restoring &&
            o->subscription.request() != gcapp::SubscriptionRequest::Checking;
        return o->configuration.subscription_build();
    }
    if (path.section == 2) {
        const auto& c = o->configuration;
        return gcapp::product_https_url(path.row == 0 ? c.support_url : path.row == 1 ? c.privacy_url : c.terms_url);
    }
    return path.section == 3 && path.row == 1;
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)path {
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    auto* o = self.owner;
    if (!o) return cell;
    UIListContentConfiguration* content = [cell defaultContentConfiguration];
    content.textProperties.numberOfLines = 0;
    content.secondaryTextProperties.numberOfLines = 0;
    const auto& c = o->configuration;
    if (path.section == 0) {
        if (path.row == 0) content.text = ns(gcapp::product_access_description(c, o->subscription, unix_now()));
        if (path.row == 1) {
            if (c.model == gcapp::PurchaseModel::Upfront) {
                content.text = @"App Store edition";
                content.secondaryText = @"Licensed for commercial use with this purchase. No subscription is required.";
            } else if (c.subscription_build()) {
                content.text = @"Monthly subscription";
                content.secondaryText = o->subscription.price().empty() ? @"Price unavailable until loaded from the App Store." : ns(o->subscription.price());
            } else {
                content.text = @"Community edition";
                content.secondaryText = @"No subscription is required by this build.";
            }
        }
        if (path.row == 2) {
            const auto problem = c.configuration_problem();
            content.text = !problem.empty() ? ns(problem) : ns(o->subscription.message());
            if (!content.text.length) content.text = @"Your documents stay under your control.";
        }
    } else if (path.section == 1) {
        content.text = @[@"Subscribe", @"Restore Purchases", @"Manage Subscription"][path.row];
        if (path.row == 0 && !o->subscription.price().empty()) content.secondaryText = ns(o->subscription.price());
    } else if (path.section == 2) {
        content.text = @[@"Support", @"Privacy Policy", @"Terms of Use"][path.row];
        if (![self actionEnabled:path]) content.secondaryText = @"Not configured in this build.";
    } else if (path.row == 0) {
        content.text = @"GLCAD";
        content.secondaryText = [NSString stringWithFormat:@"Version %@ (%@)",
            ns(info_string(@"CFBundleShortVersionString")), ns(info_string(@"CFBundleVersion"))];
    } else content.text = @"Licenses and acknowledgments";
    const BOOL enabled = [self actionEnabled:path];
    cell.selectionStyle = enabled ? UITableViewCellSelectionStyleDefault : UITableViewCellSelectionStyleNone;
    if (path.section == 1 || path.section == 2 || (path.section == 3 && path.row == 1)) {
        content.textProperties.color = enabled ? self.view.tintColor : UIColor.secondaryLabelColor;
        cell.accessibilityTraits = UIAccessibilityTraitButton | (enabled ? 0 : UIAccessibilityTraitNotEnabled);
    }
    cell.contentConfiguration = content;
    return cell;
}
- (void)openURL:(NSString*)urlString {
    NSURL* url = [NSURL URLWithString:urlString];
    if (!url) return;
    __weak GCProductController* weakSelf = self;
    [UIApplication.sharedApplication openURL:url options:@{} completionHandler:^(BOOL success) {
        if (success || !weakSelf) return;
        UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Unable to open link"
            message:@"Please check your connection and try again." preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [weakSelf presentViewController:alert animated:YES completion:nil];
    }];
}
- (void)showLicenses {
    NSMutableString* text = [NSMutableString string];
    NSURL* root = NSBundle.mainBundle.resourceURL;
    NSDirectoryEnumerator<NSURL*>* files = [NSFileManager.defaultManager enumeratorAtURL:root
        includingPropertiesForKeys:@[NSURLIsRegularFileKey] options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil];
    for (NSURL* file in files) {
        NSString* relative = [file.path substringFromIndex:root.path.length];
        NSString* leaf = file.lastPathComponent.lowercaseString;
        if (![relative hasPrefix:@"/licenses/"] && ![leaf hasPrefix:@"license"] &&
            ![leaf isEqualToString:@"third_party_notices.md"]) continue;
        NSNumber* regular = nil;
        [file getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
        if (!regular.boolValue) continue;
        NSString* body = [NSString stringWithContentsOfURL:file encoding:NSUTF8StringEncoding error:nil];
        if (body.length) [text appendFormat:@"%@\n\n%@\n\n", relative, body];
    }
    if (!text.length) [text appendString:@"License notices have not been bundled in this development build."];
    UIViewController* page = [[UIViewController alloc] init];
    page.title = @"Licenses";
    UITextView* view = [[UITextView alloc] init];
    view.editable = NO;
    view.selectable = YES;
    view.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    view.adjustsFontForContentSizeCategory = YES;
    view.textContainerInset = UIEdgeInsetsMake(16, 16, 16, 16);
    view.text = text;
    page.view = view;
    [self.navigationController pushViewController:page animated:YES];
}
- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (![self actionEnabled:path]) return;
    if (path.section == 1) {
        if (path.row == 0) gc_store_purchase();
        else if (path.row == 1) gc_store_restore();
        else [self openURL:@"https://apps.apple.com/account/subscriptions"];
    } else if (path.section == 2) {
        const auto& c = self.owner->configuration;
        [self openURL:ns(path.row == 0 ? c.support_url : path.row == 1 ? c.privacy_url : c.terms_url)];
    } else if (path.section == 3) [self showLicenses];
}
@end

static GCProductCoordinator* productCoordinator;
static void store_event(int32_t event, const char* product, const char* message,
                        int64_t until, uint32_t flags, void* context) {
    GCProductCoordinator* owner = (__bridge GCProductCoordinator*)context;
    owner->subscription.receive(event, product, message, until, flags);
    [owner notifyAccess];
    [owner.screen.tableView reloadData];
}
extern "C" void gc_product_start(void* presenter, GcProductAccessCallback callback, void* context) {
    gc_product_stop();
    productCoordinator = [[GCProductCoordinator alloc] init];
    auto* o = productCoordinator;
    o.presenter = (__bridge UIViewController*)presenter;
    o->accessCallback = callback;
    o->accessContext = context;
    o->lastAccess = -1;
    o->welcomePending = ![NSUserDefaults.standardUserDefaults boolForKey:@"GLCADWelcomeDismissed.v1"];
    NSDictionary<NSString*, NSString*>* environment = NSProcessInfo.processInfo.environment;
    if ([environment[@"GLCAD_IOS_NATIVE_SMOKE"] boolValue] ||
        [environment[@"GLCAD_IOS_STOREKIT_SMOKE"] boolValue] ||
        [environment[@"GLCAD_IOS_STOREKIT_TEST_HOST"] boolValue]) o->welcomePending = false;
    id paid = [NSBundle.mainBundle objectForInfoDictionaryKey:@"GLCADPaidBuild"];
    o->configuration.model = gcapp::purchase_model_from_info(info_string(@"GLCADPurchaseModel"),
        [paid respondsToSelector:@selector(boolValue)] && [paid boolValue]);
    o->configuration.subscription_product_id = info_string(@"GLCADSubscriptionProductID");
    o->configuration.support_url = info_string(@"GLCADSupportURL");
    o->configuration.privacy_url = info_string(@"GLCADPrivacyURL");
    o->configuration.terms_url = info_string(@"GLCADTermsURL");
    o->subscription.configure(o->configuration.subscription_build() ? o->configuration.subscription_product_id : "");
    [o notifyAccess];
    if (o->configuration.subscription_build())
        gc_store_initialize(o->subscription.product_id().c_str(), store_event, (__bridge void*)o);
    __weak GCProductCoordinator* weakOwner = o;
    o.expirationTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer*) {
        [weakOwner notifyAccess];
        [weakOwner showWelcomeIfNeeded];
    }];
}
extern "C" void gc_product_show(void) { [productCoordinator show]; }
extern "C" void gc_product_refresh(void) { gc_store_refresh(); [productCoordinator notifyAccess]; }
extern "C" void gc_product_stop(void) {
    gc_store_stop();
    [productCoordinator.expirationTimer invalidate];
    [productCoordinator.screen dismissViewControllerAnimated:NO completion:nil];
    productCoordinator = nil;
}
extern "C" int gc_product_edit_allowed(void) {
    if (!productCoordinator) return 0;
    return gcapp::product_edit_allowed(productCoordinator->configuration, productCoordinator->subscription, unix_now());
}
extern "C" void gc_product_set_sample_callback(GcProductSampleCallback callback, void* context) {
    if (!productCoordinator) return;
    productCoordinator->sampleCallback = callback;
    productCoordinator->sampleContext = context;
}
