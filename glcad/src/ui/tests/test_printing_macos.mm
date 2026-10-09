#import <Cocoa/Cocoa.h>
#include "test.hpp"

TEST(macos_printing_decodes_native_png_with_the_launch_environment) {
    @autoreleasepool {
        // A broad DYLD_LIBRARY_PATH override breaks ImageIO's private libpng (SIGBUS).
        NSData* bytes = [[NSData alloc] initWithBase64EncodedString:
            @"iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAF0lEQVR4nGP4z8DQwPAfCBkY/gMBAwMAP9kHeswQB0QAAAAASUVORK5CYII=" options:0];
        NSBitmapImageRep* image = [NSBitmapImageRep imageRepWithData:bytes];
        CHECK(image != nil);
        CHECK(image.pixelsWide == 2 && image.pixelsHigh == 2);
    }
}

