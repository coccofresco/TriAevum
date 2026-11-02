/* Copyright TriAevum contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Dedicated bundle entry: only this signature changes when sealing resources.
 * The catalogued Forge/runtime binaries retain their exact signed hashes.
 */
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char *path = malloc((size_t)size + sizeof("../Resources/TriAevumForge"));
    if (path == NULL || _NSGetExecutablePath(path, &size) != 0) {
        free(path);
        return 1;
    }
    char *leaf = strrchr(path, '/');
    if (leaf == NULL) {
        free(path);
        return 1;
    }
    strcpy(leaf + 1, "../Resources/TriAevumForge");
    argv[0] = path;
    /* Finder may supply a legacy process-serial-number argument. */
    if (argc == 2 && strncmp(argv[1], "-psn_", 5) == 0) {
        argv[1] = NULL;
    }
    execv(path, argv);
    perror("Cannot start TriAevum Forge");
    free(path);
    return 1;
}
