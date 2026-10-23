#include "v110_address.h"

#include <iostream>

namespace {

using Mm3dDekui::V110;
using Mm3dDekui::V110Branch;

// dekui names both addresses of these four sites (its Restoration and its
// vanilla variants): the table must agree with it.
static_assert(V110(0x0021E55CU) == 0x0021E34CU, "difficulty damage");
static_assert(V110(0x0022074CU) == 0x0022053CU, "difficulty spawn");
static_assert(V110(0x0018D570U) == 0x0018D414U, "camera projection");
static_assert(V110(0x0018D584U) == 0x0018D428U, "camera projection");
// Translated sites whose v1.1 words were checked against code.bin.
static_assert(V110(0x005A745CU) == 0x005A75CCU, "song list navigation");
static_assert(V110(0x002C1634U) == 0x002C157CU, "change rupees");
// A call whose target lies in a differently shifted stretch: the BL is
// re-encoded (prices, 0x00299568 -> 0x00299404 in v1.1).
static_assert(V110Branch(0xEB00A031U, 0x00299568U) == 0xEB00A05CU, "prices call");
// Data and BSS keep their addresses; Restoration's appended code and
// rodata have no translation.
static_assert(V110(0x0072F5C0U) == 0x0072F5C0U, "game context");
static_assert(V110(0x006A0148U) == 0x006A0148U, "letterbox alpha");
static_assert(V110(0x007D0910U) == 0U, "appended Restoration code");
static_assert(V110(0x0062F000U) == 0U, "rodata");

} // namespace

int main() {
    std::cout << "mm3d_v110_address_tests: ok\n";
    return 0;
}
