#ifndef __MAC_UTILS_H
#define __MAC_UTILS_H

namespace Slic3r {

bool is_macos_support_boost_add_file_log();
int  is_mac_version_15();
// Begin / end a "user initiated" activity so macOS does not throttle the process (App Nap) while slicing.
// Calls may nest. Disabled when FLASH_DISABLE_APPNAP_GUARD is set.
void mac_slicing_activity_begin();
void mac_slicing_activity_end();
}

#endif
