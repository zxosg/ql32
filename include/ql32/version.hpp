#pragma once

// Single source of truth for the firmware/host version. Keep in step with
// project(ql32 VERSION ...) in CMakeLists.txt and CHANGELOG.md.
#define QL32_VERSION_STRING "0.1.0"

#if defined(QL32_COMPACT_CODE)
#define QL32_VERSION_FEATURES " compact"
#else
#define QL32_VERSION_FEATURES ""
#endif
