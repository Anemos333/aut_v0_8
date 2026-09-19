from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
text = path.read_text()

old_decl = """    std::array<std::uint64_t, 96> residualLineCacheKeys {};
    std::array<float, 96> residualLineCacheValues {};
    std::size_t residualLineCacheCount = 0;
"""
new_decl = """    // RESIDUAL_LINE_CACHE_INDEX_V1
    // Keep the existing 96-entry semantic cap, but index those exact-bit keys
    // through a fixed open-address table instead of rescanning every prior key.
    // Cache hits still return the identical stored float; first requests still
    // execute the unchanged spectral-line arithmetic below.
    static constexpr std::size_t residualLineCacheSemanticCapacity = 96;
    static constexpr std::size_t residualLineCacheTableSize = 128;
    static constexpr std::size_t residualLineCacheTableMask =
        residualLineCacheTableSize - 1;
    std::array<std::uint64_t, residualLineCacheTableSize> residualLineCacheKeys {};
    std::array<float, residualLineCacheTableSize> residualLineCacheValues {};
    std::array<std::uint8_t, residualLineCacheTableSize> residualLineCacheOccupied {};
    std::size_t residualLineCacheCount = 0;
"""
if text.count(old_decl) != 1:
    raise SystemExit(f"expected one cache declaration, got {text.count(old_decl)}")
text = text.replace(old_decl, new_decl, 1)

old_lookup = """        for (std::size_t cacheIndex = 0;
             cacheIndex < residualLineCacheCount;
             ++cacheIndex)
        {
            if (residualLineCacheKeys[cacheIndex] == cacheKey)
                return residualLineCacheValues[cacheIndex];
        }

        double real = 0.0;
"""
new_lookup = """        const std::size_t cacheStart = static_cast<std::size_t>(
            (cacheKey * 11400714819323198485ull) >> 57)
            & residualLineCacheTableMask;
        for (std::size_t probe = 0; probe < residualLineCacheTableSize; ++probe)
        {
            const std::size_t cacheIndex =
                (cacheStart + probe) & residualLineCacheTableMask;
            if (residualLineCacheOccupied[cacheIndex] == 0u)
                break;
            if (residualLineCacheKeys[cacheIndex] == cacheKey)
                return residualLineCacheValues[cacheIndex];
        }

        double real = 0.0;
"""
if text.count(old_lookup) != 1:
    raise SystemExit(f"expected one cache lookup, got {text.count(old_lookup)}")
text = text.replace(old_lookup, new_lookup, 1)

old_insert = """        if (residualLineCacheCount < residualLineCacheKeys.size())
        {
            residualLineCacheKeys[residualLineCacheCount] = cacheKey;
            residualLineCacheValues[residualLineCacheCount] = value;
            ++residualLineCacheCount;
        }
        return value;
"""
new_insert = """        if (residualLineCacheCount < residualLineCacheSemanticCapacity)
        {
            for (std::size_t probe = 0; probe < residualLineCacheTableSize; ++probe)
            {
                const std::size_t cacheIndex =
                    (cacheStart + probe) & residualLineCacheTableMask;
                if (residualLineCacheOccupied[cacheIndex] == 0u)
                {
                    residualLineCacheKeys[cacheIndex] = cacheKey;
                    residualLineCacheValues[cacheIndex] = value;
                    residualLineCacheOccupied[cacheIndex] = 1u;
                    ++residualLineCacheCount;
                    break;
                }
            }
        }
        return value;
"""
if text.count(old_insert) != 1:
    raise SystemExit(f"expected one cache insertion, got {text.count(old_insert)}")
text = text.replace(old_insert, new_insert, 1)

path.write_text(text)
print("RESIDUAL_LINE_CACHE_INDEX_PATCH=APPLIED")
