from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
text = path.read_text()

anchor = """    bool residualHannReady = false;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
"""
replacement = """    bool residualHannReady = false;

    // RESIDUAL_ENERGY_EXACT_REUSE_V1
    // signalEnergy and windowEnergy are independent of cyclesPerSample.
    // The first unique spectral line still computes them at the exact golden
    // points and in the exact golden order. Later unique lines reuse only the
    // already-rounded double results from that first line.
    bool residualEnergyReady = false;
    double residualSignalEnergy = 0.0;
    double residualWindowEnergy = 0.0;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
"""
if text.count(anchor) != 1:
    raise SystemExit(f"expected one Hann anchor, got {text.count(anchor)}")
text = text.replace(anchor, replacement, 1)

old = """        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = 0.0;
        double windowEnergy = 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            double window = 0.0;
            if (!residualHannReady)
            {
                window = 0.5 - 0.5 * std::cos(
                    twoPi * static_cast<double>(index) / denominatorN);
                residualHannWindow_[static_cast<std::size_t>(index)] = window;
            }
            else
            {
                window = residualHannWindow_[static_cast<std::size_t>(index)];
            }

            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        residualHannReady = true;
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
"""
new = """        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = residualEnergyReady ? residualSignalEnergy : 0.0;
        double windowEnergy = residualEnergyReady ? residualWindowEnergy : 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            double window = 0.0;
            if (!residualHannReady)
            {
                window = 0.5 - 0.5 * std::cos(
                    twoPi * static_cast<double>(index) / denominatorN);
                residualHannWindow_[static_cast<std::size_t>(index)] = window;
            }
            else
            {
                window = residualHannWindow_[static_cast<std::size_t>(index)];
            }

            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            if (!residualEnergyReady)
            {
                signalEnergy += sample * sample;
                windowEnergy += window * window;
            }
        }
        if (!residualEnergyReady)
        {
            residualSignalEnergy = signalEnergy;
            residualWindowEnergy = windowEnergy;
            residualEnergyReady = true;
        }
        residualHannReady = true;
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
"""
if text.count(old) != 1:
    raise SystemExit(f"expected one residual-line body, got {text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)
print("RESIDUAL_ENERGY_EXACT_REUSE_PATCH=APPLIED")
