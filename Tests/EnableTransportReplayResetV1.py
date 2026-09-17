from pathlib import Path

path = Path('Source/LivePitchProcessor.h')
text = path.read_text()

marker = 'TRANSPORT_REPLAY_DETERMINISM_V1'
if marker in text:
    print(marker + '=already_materialized')
    raise SystemExit(0)

old_prepare = '''        voiceEvidenceAnalyzer_.prepare(sampleRate_, maximumBlockSize_, channelCount_);\n        voiceEvidencePrimed_ = false;\n        activeModeIndex_.store(toModeIndex(latencyMode),\n                               std::memory_order_release);\n'''
new_prepare = '''        voiceEvidenceAnalyzer_.prepare(sampleRate_, maximumBlockSize_, channelCount_);\n        voiceEvidencePrimed_ = false;\n        hostTransportHistoryValid_ = false;\n        hostPpqHistoryValid_ = false;\n        expectedNextHostSample_ = 0;\n        expectedNextHostPpq_ = 0.0;\n        activeModeIndex_.store(toModeIndex(latencyMode),\n                               std::memory_order_release);\n'''
assert old_prepare in text
text = text.replace(old_prepare, new_prepare, 1)

old_reset = '''        voiceEvidenceAnalyzer_.reset();\n        voiceEvidencePrimed_ = false;\n    }\n'''
new_reset = '''        voiceEvidenceAnalyzer_.reset();\n        voiceEvidencePrimed_ = false;\n        hostTransportHistoryValid_ = false;\n        hostPpqHistoryValid_ = false;\n        expectedNextHostSample_ = 0;\n        expectedNextHostPpq_ = 0.0;\n    }\n'''
assert old_reset in text
text = text.replace(old_reset, new_reset, 1)

old_set = '''    void setTempoHostPosition(const CreativeTempo::HostPosition& position) noexcept\n    {\n        tempoHostPosition_ = position;\n    }\n'''
new_set = '''    void setTempoHostPosition(const CreativeTempo::HostPosition& position) noexcept\n    {\n        // TRANSPORT_REPLAY_DETERMINISM_V1\n        // A non-looping seek/restart is a physical observation discontinuity.\n        // Detector, correction, voice-evidence and renderer state from the old\n        // timeline must not seed the new playback. This is transport hygiene,\n        // never a confidence/F0 gate and never runs during contiguous playback.\n        bool transportDiscontinuity = false;\n        if (position.isPlaying && !position.isLooping)\n        {\n            if (hostTransportHistoryValid_ && position.hasTimeInSamples)\n            {\n                const auto error = std::llabs(position.timeInSamples\n                                              - expectedNextHostSample_);\n                const auto tolerance = static_cast<std::int64_t>(\n                    std::max(4, std::max(1, position.numberOfSamples) * 2));\n                transportDiscontinuity = error > tolerance;\n            }\n            else if (!position.hasTimeInSamples\n                     && hostPpqHistoryValid_\n                     && position.hasPpq\n                     && position.hasBpm\n                     && std::isfinite(position.ppqAtBlockStart)\n                     && std::isfinite(position.bpm)\n                     && position.bpm > 1.0)\n            {\n                const double expectedTravel = position.bpm\n                    / (60.0 * std::max(8000.0, sampleRate_))\n                    * static_cast<double>(std::max(1, position.numberOfSamples));\n                const double tolerance = std::max(0.01, expectedTravel * 3.0);\n                transportDiscontinuity = std::abs(position.ppqAtBlockStart\n                                                  - expectedNextHostPpq_) > tolerance;\n            }\n        }\n\n        if (transportDiscontinuity)\n            reset();\n\n        tempoHostPosition_ = position;\n\n        if (position.isPlaying && position.hasTimeInSamples)\n        {\n            expectedNextHostSample_ = position.timeInSamples\n                + static_cast<std::int64_t>(std::max(0, position.numberOfSamples));\n            hostTransportHistoryValid_ = true;\n        }\n\n        if (position.isPlaying\n            && position.hasPpq\n            && position.hasBpm\n            && std::isfinite(position.ppqAtBlockStart)\n            && std::isfinite(position.bpm)\n            && position.bpm > 1.0)\n        {\n            expectedNextHostPpq_ = position.ppqAtBlockStart\n                + position.bpm / (60.0 * std::max(8000.0, sampleRate_))\n                    * static_cast<double>(std::max(0, position.numberOfSamples));\n            hostPpqHistoryValid_ = true;\n        }\n    }\n'''
assert old_set in text
text = text.replace(old_set, new_set, 1)

old_fields = '''    int channelCount_ = 1;\n    CreativeTempo::HostPosition tempoHostPosition_;\n};\n'''
new_fields = '''    int channelCount_ = 1;\n    CreativeTempo::HostPosition tempoHostPosition_;\n    bool hostTransportHistoryValid_ = false;\n    bool hostPpqHistoryValid_ = false;\n    std::int64_t expectedNextHostSample_ = 0;\n    double expectedNextHostPpq_ = 0.0;\n};\n'''
assert old_fields in text
text = text.replace(old_fields, new_fields, 1)

path.write_text(text)
print(marker + '=materialized')
