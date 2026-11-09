// The build-path remapping (CMake/build_path_remapping.cmake) reaches this
// translation unit like every product one: __FILE__, which spdlog prints in
// each log line, must be repository-relative, never the build machine's path.
#include <cstdio>
#include <cstring>

namespace {
bool Absolute(const char* path) {
    const bool drive = ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
                       path[1] == ':';
    return drive || path[0] == '/' || path[0] == '\\';
}
} // namespace

int main() {
    const char* file = __FILE__;
    std::printf("__FILE__=%s\n", file);
    int failures = 0;
    if (Absolute(file)) {
        std::printf("FAIL: __FILE__ is an absolute build-machine path\n");
        ++failures;
    }
    if (std::strncmp(file, "tools", 5) != 0) {
        std::printf("FAIL: __FILE__ does not start at the repository root\n");
        ++failures;
    }
    if (std::strstr(file, "build_path_remap_tests.cpp") == nullptr) {
        std::printf("FAIL: __FILE__ lost its file name\n");
        ++failures;
    }
    std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
