#include "client_apple.hpp"

#import <Foundation/Foundation.h>

#include <atomic>
#include <memory>

@interface GLCADHttpDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) const gai::detail::Receiver* receiver;
@property(nonatomic) std::shared_ptr<gai::detail::HttpReply> reply;
@property(nonatomic, strong) dispatch_semaphore_t done;
@end

@implementation GLCADHttpDelegate
- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))handler {
    if ([response isKindOfClass:[NSHTTPURLResponse class]])
        self.reply->status = static_cast<int>(((NSHTTPURLResponse*)response).statusCode);
    self.reply->reached = true;
    handler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)task
    didReceiveData:(NSData*)data {
    const auto* bytes = static_cast<const char*>(data.bytes);
    const std::size_t len = data.length;
    if (self.receiver && *self.receiver && self.reply->status == 200) {
        if (!(*self.receiver)(bytes, len)) [task cancel];
    } else {
        self.reply->body.append(bytes, len);
    }
}
- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
    if (error && error.code != NSURLErrorCancelled) {
        self.reply->error = error.localizedDescription.UTF8String ?: "network error";
        if (!self.reply->status) self.reply->reached = false;
    }
    dispatch_semaphore_signal(self.done);
}
@end

namespace gai::detail {

HttpReply apple_request(const std::string& url, const std::string& method,
                        const std::vector<std::pair<std::string, std::string>>& headers,
                        const std::string& body, int connect_timeout_s, int idle_timeout_s,
                        const Receiver* receiver) {
    auto reply = std::make_shared<HttpReply>();
    @autoreleasepool {
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!u) {
            reply->error = "invalid URL";
            return *reply;
        }
        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:u];
        req.HTTPMethod = [NSString stringWithUTF8String:method.c_str()];
        for (const auto& [k, v] : headers)
            [req setValue:[NSString stringWithUTF8String:v.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:k.c_str()]];
        if (!body.empty()) req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

        NSURLSessionConfiguration* cfg = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        cfg.timeoutIntervalForRequest = idle_timeout_s > 0 ? idle_timeout_s : 120;
        cfg.timeoutIntervalForResource = 24 * 3600;
        cfg.waitsForConnectivity = NO;
        (void)connect_timeout_s;

        GLCADHttpDelegate* delegate = [GLCADHttpDelegate new];
        delegate.receiver = receiver;
        delegate.reply = reply;
        delegate.done = dispatch_semaphore_create(0);

        NSOperationQueue* q = [NSOperationQueue new];
        q.maxConcurrentOperationCount = 1;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:cfg delegate:delegate delegateQueue:q];
        [[session dataTaskWithRequest:req] resume];
        dispatch_semaphore_wait(delegate.done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];
    }
    return *reply;
}

}
