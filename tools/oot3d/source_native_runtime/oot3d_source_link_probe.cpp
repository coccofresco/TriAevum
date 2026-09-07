#include <cstdint>

extern "C" void nnMain(std::uint32_t, std::uint32_t);

int main() {
    auto* volatile entry = &nnMain;
    return entry == nullptr;
}
