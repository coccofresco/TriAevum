#pragma once

#include <cassert>

#define ASSERT(condition) assert(condition)
#define DEBUG_ASSERT(condition) assert(condition)
#define ASSERT_MSG(condition, ...) assert(condition)
#define UNIMPLEMENTED() ((void)0)
#define UNIMPLEMENTED_MSG(...) ((void)0)
#define UNREACHABLE_MSG(...) assert(false)
