#include "oot3d_ui/ui_backend.h"
#include "oot3d_ui/ui_native_workflow_closure.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    using namespace oot3d::ui;

    std::string error;
    const UiBackendProfile profile = BuildOot3dNativeUiProfile();
    bool valid = Check(ValidateUiBackendProfile(profile, &error), error.c_str());
    valid &= Check(Oot3dNativeUiFunctionContracts().size() ==
                       kUiNativeFunctionContractCount,
                   "native UI primary contract count drifted");
    valid &= Check(Oot3dNativeWorkflowFunctions().size() ==
                       kOot3dNativeWorkflowFunctionCount,
                   "native UI workflow function count drifted");
    valid &= Check(Oot3dNativeWorkflowEdges().size() ==
                       kOot3dNativeWorkflowEdgeCount,
                   "native UI workflow edge count drifted");

    for (std::size_t index = 0; index < kUiSubsystemCount; ++index) {
        const auto subsystem = static_cast<UiSubsystem>(index);
        const UiFramePlan plan = BuildFramePlan(profile, subsystem);
        valid &= Check(plan.use_guest_content && plan.run_guest_mechanics &&
                           plan.forward_input_to_guest && plan.run_guest_presentation,
                       "native UI profile stopped retaining a guest responsibility");
        valid &= Check(!plan.run_host_content && !plan.run_host_mechanics &&
                           !plan.route_input_to_host && !plan.run_host_presentation,
                       "native UI profile unexpectedly enabled a host responsibility");
    }

    if (!valid) {
        return 1;
    }
    std::cout << "native_ui_contract=ok primary="
              << kUiNativeFunctionContractCount << " workflow_functions="
              << kOot3dNativeWorkflowFunctionCount << " workflow_edges="
              << kOot3dNativeWorkflowEdgeCount << '\n';
    return 0;
}
