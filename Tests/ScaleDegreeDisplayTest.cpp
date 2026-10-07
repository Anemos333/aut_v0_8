#include "ScaleDegreeDisplay.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void checkScale(const std::vector<double>& ratios, double equave, double root)
{
    neumaton::pitch::ScaleQuantizer quantizer;
    require(quantizer.setScale(ratios.data(), static_cast<int>(ratios.size()), root, equave), "setScale");
    // Validate the UI against real quantizer targets across registers, including
    // duplicate/unsorted scale input and non-octave equaves.
    for (int step = -800; step <= 800; ++step)
    {
        const double input = root * std::exp2(static_cast<double>(step) / 240.0);
        const auto target = quantizer.quantize(input, 1.0f, 0.0f);
        const auto display = neumaton::ui::describeScaleTarget(
            static_cast<float>(target.targetHz), root, equave, ratios, false);
        require(display.degree > 0 && display.degree <= display.count, "target degree is missing");
        require(display.count == quantizer.size(), "degree count differs from engine");
    }
}
} // namespace

int main()
{
    const std::vector<double> major {1.0, std::exp2(2.0/12), std::exp2(4.0/12),
        std::exp2(5.0/12), std::exp2(7.0/12), std::exp2(9.0/12), std::exp2(11.0/12), 2.0};
    checkScale(major, 2.0, 261.625565);
    checkScale({1.5, 2.0, 1.0, 1.25, 1.25, 1.75}, 2.0, 432.0);
    checkScale({1.0, std::pow(3.0, 1.0/3), std::pow(3.0, 2.0/3), 3.0}, 3.0, 220.0);
    checkScale({1.0}, 2.0, 440.0);
    const double root = 261.625565;
    auto readout = neumaton::ui::describeScaleTarget(root * major[3], root, 2.0, major, false);
    require(readout.degree == 4 && readout.count == 7, "known degree must be 4/7");
    readout = neumaton::ui::describeScaleTarget(root * major[3] / 4, root, 2.0, major, true);
    require(readout.degree == 4 && readout.held, "held target must retain its degree across registers");
    require(neumaton::ui::describeScaleTarget(0, root, 2, major, false).degree == 0, "no target");
    require(neumaton::ui::describeScaleTarget(root * std::exp2(1.0/12), root, 2, major, true).degree == 0,
            "previous scale target must not be relabelled");
    require(neumaton::ui::describeScaleTarget(root, 0, 2, major, false).degree == 0, "invalid centre");
    require(neumaton::ui::describeScaleTarget(root, root, 1, major, false).degree == 0, "invalid equave");
    require(neumaton::ui::describeScaleTarget(std::numeric_limits<double>::quiet_NaN(), root, 2, major, false).degree == 0, "invalid target");
    std::cout << "PASS: scale-degree presentation matches V1 targets; held, stale and invalid targets checked\n";
}
