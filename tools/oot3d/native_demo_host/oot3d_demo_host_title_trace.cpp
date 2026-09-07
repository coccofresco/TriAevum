#include "oot3d_demo_host_title_trace.h"

#include <stdexcept>

#include "oot3d_demo_host_io.h"
#include "oot3d_title_intro_trace.h"

void RunTitleIntroTrace(const Args& args) {
    if (args.OutputPath.empty()) {
        throw std::runtime_error("--title-intro-trace requires --output <trace.json>");
    }
    WriteJsonFile(args.OutputPath, BuildTitleIntroOpeningFrameTrace(args));
}
