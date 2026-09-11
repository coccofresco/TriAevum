#pragma once

// Scoped to the donor's host-side SSSR targets, never the renderer or gameplay.
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <locale>
#include <codecvt>
#include <FidelityFX/host/ffx_util.h>

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

inline int wcscpy_s(wchar_t* destination, size_t capacity, const wchar_t* source) {
    if (!destination || capacity == 0) return EINVAL;
    if (!source) {
        destination[0] = L'\0';
        return EINVAL;
    }
    const size_t length = std::wcslen(source);
    if (length >= capacity) {
        destination[0] = L'\0';
        return ERANGE;
    }
    std::wmemcpy(destination, source, length + 1);
    return 0;
}

template <size_t Capacity>
inline int wcscpy_s(wchar_t (&destination)[Capacity], const wchar_t* source) {
    return wcscpy_s(destination, Capacity, source);
}

inline int strcpy_s(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) return EINVAL;
    if (!source || std::strlen(source) >= capacity) {
        destination[0] = '\0';
        return source ? ERANGE : EINVAL;
    }
    std::memcpy(destination, source, std::strlen(source) + 1);
    return 0;
}

template <typename... Args>
inline int sprintf_s(char* destination, size_t capacity, const char* format, Args... args) {
    if (!destination || capacity == 0 || !format) return -1;
    const int length = std::snprintf(destination, capacity, format, args...);
    if (length < 0 || static_cast<size_t>(length) >= capacity) {
        destination[0] = '\0';
        return -1;
    }
    return length;
}
