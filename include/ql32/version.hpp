#pragma once

// Single source of truth for the firmware/host version. CMakeLists.txt reads
// this value; keep CHANGELOG.md in step.
#define QL32_VERSION_STRING "0.1.0"

#if defined(QL32_COMPACT_CODE)
#define QL32_VERSION_FEATURES " compact"
#else
#define QL32_VERSION_FEATURES ""
#endif
