#pragma once

#include <cassert>
#include <stdexcept>

#define ASSERT(condition) assert(condition)
#define DEBUG_ASSERT(condition) assert(condition)
#define UNREACHABLE() throw std::runtime_error("unreachable PICA shader path")
#define LOG_ERROR(category, ...) ((void)0)
#define LOG_INFO(category, ...) ((void)0)
