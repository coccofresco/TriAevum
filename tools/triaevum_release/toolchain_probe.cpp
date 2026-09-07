#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <thread>

thread_local unsigned counter = 0;

double Callback(double value) {
    ++counter;
    return value * 2.0 + 1.0;
}

int main() {
    std::vector<unsigned> values{1, 2, 3};
    std::span<const unsigned> view(values);
    for (const auto value : view) counter += value;
    bool workerPassed = false;
    std::thread worker([&] {
        double (*volatile callback)(double) = Callback;
        workerPassed = counter == 0 && callback(0.25) == 1.5 && counter == 1;
    });
    worker.join();
    if (!workerPassed || counter != 6 || !std::signbit(-0.0)) return 2;
    try {
        throw std::runtime_error("probe");
    } catch (const std::runtime_error&) {
        const nlohmann::json state{{"counter", counter}};
        return state.at("counter").get<unsigned>() == 6 ? 0 : 1;
    }
}
