/*
 * Copyright © 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted for the OOT3D Switch Vulkan probe from NXVK's
 * switch/smoke/nvk_compat.c at commit 69ec283dbda64e65347a36274efb349122e85363.
 */
#include <switch.h>

#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {

ssize_t getrandom(void* buffer, size_t size, unsigned int flags) {
    (void)flags;
    if (buffer == nullptr && size != 0) {
        errno = EFAULT;
        return -1;
    }
    randomGet(buffer, size);
    return static_cast<ssize_t>(size);
}

int posix_memalign(void** result, size_t alignment, size_t size) {
    if (alignment < sizeof(void*) ||
        (alignment & (alignment - 1U)) != 0) {
        return EINVAL;
    }
    void* allocation = memalign(alignment, size);
    if (allocation == nullptr) {
        return ENOMEM;
    }
    *result = allocation;
    return 0;
}

uid_t getuid() { return 0; }
uid_t geteuid() { return 0; }
gid_t getgid() { return 0; }
gid_t getegid() { return 0; }

long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_PHYS_PAGES:
        return (3L * 1024L * 1024L * 1024L) / 4096L;
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN: {
        uint64_t core_mask = 0;
        const Result result = svcGetInfo(&core_mask, InfoType_CoreMask,
                                         CUR_PROCESS_HANDLE, 0);
        return R_SUCCEEDED(result) && core_mask != 0
                   ? static_cast<long>(__builtin_popcountll(core_mask))
                   : 1L;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int regcomp(regex_t* expression, const char* pattern, int flags) {
    (void)pattern;
    (void)flags;
    if (expression != nullptr) {
        expression->re_nsub = 0;
    }
    // newlib does not provide a regex engine here.  Report the missing
    // capability instead of claiming a successful compile that can never
    // match; callers can then disable the dependent cache/configuration path.
    return REG_BADPAT;
}

int regexec(const regex_t* expression, const char* text, size_t match_count,
            regmatch_t matches[], int flags) {
    (void)expression;
    (void)text;
    (void)match_count;
    (void)matches;
    (void)flags;
    return REG_NOMATCH;
}

void regfree(regex_t* expression) { (void)expression; }

int pthread_sigmask(int operation, const sigset_t* set, sigset_t* previous) {
    (void)operation;
    (void)set;
    if (previous != nullptr) {
        memset(previous, 0, sizeof(*previous));
    }
    return 0;
}

int flock(int descriptor, int operation) {
    (void)descriptor;
    (void)operation;
    errno = ENOTSUP;
    return -1;
}

int dirfd(DIR* directory) {
    (void)directory;
    errno = ENOTSUP;
    return -1;
}

int fstatat(int descriptor, const char* path, struct stat* status, int flags) {
    (void)descriptor;
    (void)path;
    (void)status;
    (void)flags;
    errno = ENOTSUP;
    return -1;
}

int getpwuid_r(uid_t user, struct passwd* entry, char* buffer,
               size_t buffer_size, struct passwd** result) {
    (void)user;
    (void)entry;
    (void)buffer;
    (void)buffer_size;
    if (result != nullptr) {
        *result = nullptr;
    }
    return ENOTSUP;
}

} // extern "C"
