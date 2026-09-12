#include <exception>
#include <iostream>

void RunNativeFrameRateTests();

int main() {
    try {
        RunNativeFrameRateTests();
        std::cout << "Presentation clock tests passed, including suspended frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
