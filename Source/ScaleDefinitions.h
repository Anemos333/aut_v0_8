#pragma once

#include <string>
#include <string_view>
#include <vector>

enum class ScaleRuntimeClass
{
    fixedScale,
    equalDivision,
    rationalHistorical,
    historicalTemperament,
    modalVariable,
    ensembleSpecific,
    reconstruction
};

enum class ScalePrecision
{
    theoreticalExact,
    theoretical,
    conventional,
    nominalVariable,
    measured,
    reconstructed,
    idealized
};

enum class TonalCenterRole
{
    tonicOrKeyCenter,
    gridAnchor,
    keyCenterAndTuningAnchor,
    referenceDegree,
    modalCenter,
    modalTonic,
    movableTonicSa,
    ensembleReference,
    reconstructionReference
};

enum class ReferenceTuningPolicy
{
    userA4,
    userReferenceHz,
    movableReferenceHz,
    movableTonicHz,
    sourceOrUserNormalized,
    ensembleMeasuredOrUserNormalized
};

// Canonical definition used by factory scales and future imported packs.
// stableId is persistence identity; display names and menu grouping may evolve.
struct ScaleInfo
{
    std::string stableId;
    std::string packId;
    std::string name;
    std::string category;
    std::string areaPeriod;
    std::string sourceUrl;
    std::string notes;

    ScaleRuntimeClass runtimeClass = ScaleRuntimeClass::fixedScale;
    ScalePrecision precision = ScalePrecision::theoreticalExact;
    TonalCenterRole centerRole = TonalCenterRole::tonicOrKeyCenter;
    ReferenceTuningPolicy referencePolicy = ReferenceTuningPolicy::userA4;

    double equaveRatio = 2.0;
    double defaultReferenceHz = 440.0;
    std::vector<double> ratios; // sorted [1.0, equaveRatio), unison included

    // Hidden entries exist only for exact migration of pre-database sessions.
    // They are not part of the 120-scale V1 corpus shown in the browser.
    bool visibleInMenu = true;
};

class ScaleDefinitions
{
public:
    static constexpr std::string_view factoryPackId = "neumaton.factory.scales.v1";
    static constexpr int databaseSchemaVersion = 1;
    static constexpr int factoryVisibleScaleCount = 120;

    static const std::vector<ScaleInfo>& getAllScales();
    static int getScaleCount();
    static int getVisibleScaleCount() noexcept { return factoryVisibleScaleCount; }
    static const ScaleInfo& getScale (int index);

    static int findScaleIndexByStableId (std::string_view stableId) noexcept;
    static const ScaleInfo* findScaleByStableId (std::string_view stableId) noexcept;

private:
    static std::vector<ScaleInfo> buildScales();
    static std::vector<ScaleInfo> scales_;
};
