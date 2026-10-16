#import <Foundation/Foundation.h>
#import "MacUtils.hpp"

namespace Slic3r {

bool is_macos_support_boost_add_file_log()
{
    if (@available(macOS 12.0, *)) {
        return true;
    } else {
        return false;
    }
}

int is_mac_version_15()
{
    if (@available(macOS 15.0, *)) {//This code runs on macOS 15 or later.
        return true;
    } else {
        return false;
    }
}

static id        s_slicing_activity = nil;
static int       s_slicing_activity_depth = 0;
static NSObject* s_slicing_activity_lock = [NSObject new];

void mac_slicing_activity_begin()
{
    static const bool disabled = getenv("FLASH_DISABLE_APPNAP_GUARD") != nullptr;
    if (disabled)
        return;
    @synchronized (s_slicing_activity_lock) {
        if (s_slicing_activity_depth++ == 0) {
            // UserInitiated keeps the process out of App Nap and timer coalescing; idle system sleep stays allowed.
            s_slicing_activity = [[NSProcessInfo processInfo]
                beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                                  reason:@"Slicing"];
        }
    }
}

void mac_slicing_activity_end()
{
    static const bool disabled = getenv("FLASH_DISABLE_APPNAP_GUARD") != nullptr;
    if (disabled)
        return;
    @synchronized (s_slicing_activity_lock) {
        if (s_slicing_activity_depth > 0 && --s_slicing_activity_depth == 0 && s_slicing_activity != nil) {
            [[NSProcessInfo processInfo] endActivity:s_slicing_activity];
            s_slicing_activity = nil;
        }
    }
}
}; // namespace Slic3r
