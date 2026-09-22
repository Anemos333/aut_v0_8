#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

enum class Kind { normal, strongSecond, missingFundamental, onsetBurst };

const char* kindName(Kind k)
{
    switch (k)
    {
        case Kind::normal: return "normal";
        case Kind::strongSecond: return "strong_second";
        case Kind::missingFundamental: return "missing_fundamental";
        case Kind::onsetBurst: return "onset_burst";
    }
    return "unknown";
}

float periodic(Kind kind, double p) noexcept
{
    if (kind == Kind::strongSecond)
        return static_cast<float>(
              0.18 * std::sin(p)
            + 1.00 * std::sin(2.0 * p + 0.17)
            + 0.35 * std::sin(3.0 * p - 0.31)
            + 0.15 * std::sin(4.0 * p + 0.49)
            + 0.08 * std::sin(5.0 * p - 0.63));

    if (kind == Kind::missingFundamental)
        return static_cast<float>(
              0.82 * std::sin(2.0 * p + 0.17)
            + 0.50 * std::sin(3.0 * p - 0.31)
            + 0.30 * std::sin(4.0 * p + 0.49)
            + 0.18 * std::sin(5.0 * p - 0.63));

    return static_cast<float>(
          0.72 * std::sin(p)
        + 0.34 * std::sin(2.0 * p + 0.17)
        + 0.21 * std::sin(3.0 * p - 0.31)
        + 0.13 * std::sin(4.0 * p + 0.49)
        + 0.08 * std::sin(5.0 * p - 0.63));
}

double cents(double measured, double target) noexcept
{
    if (!(measured > 0.0) || !(target > 0.0))
        return 1.0e9;
    return 1200.0 * std::log2(measured / target);
}

struct Metrics
{
    std::array<double,4> firstPathAny {{-1,-1,-1,-1}};
    std::array<double,4> firstPathCorrect {{-1,-1,-1,-1}};
    double firstAnyCorrect = -1.0;
    double firstOwnerCorrect = -1.0;
    double firstResolvedCorrect = -1.0;
    double firstMeasurement = -1.0;
    double firstMeasurementCorrect = -1.0;
    double firstValidCorrect = -1.0;
    int wrongMeasurementsBeforeCorrect = 0;
    int octaveMeasurementsBeforeCorrect = 0;
    int hops = 0;
};

bool isCorrect(double f, double target) noexcept
{
    return std::abs(cents(f,target)) <= 1.5;
}

bool isOctaveFamilyError(double f, double target) noexcept
{
    if (!(f > 0.0))
        return false;
    const double oct = std::log2(f/target);
    const int n = static_cast<int>(std::lround(oct));
    return n != 0 && std::abs(n) <= 3
        && std::abs(1200.0 * (oct - static_cast<double>(n))) <= 85.0;
}

void inspectHop(ModernPitchEngine::MultiRatePitchTracker& t,
                const ModernPitchEngine::PitchObservation& o,
                double elapsedMs,
                double target,
                Metrics& m)
{
    ++m.hops;
    const std::array<const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot*,4> slots {{
        &t.fullRateCandidate_, &t.halfRateCandidate_,
        &t.quarterRateCandidate_, &t.eighthRateCandidate_
    }};

    for (int p=0;p<4;++p)
    {
        const auto& slot=*slots[static_cast<std::size_t>(p)];
        const auto& c=slot.candidate;
        if (!c.valid || slot.ageInHops != 0 || !(c.frequencyHz>0.0f))
            continue;

        if (m.firstPathAny[static_cast<std::size_t>(p)] < 0.0)
            m.firstPathAny[static_cast<std::size_t>(p)] = elapsedMs;

        if (isCorrect(c.frequencyHz,target))
        {
            if (m.firstPathCorrect[static_cast<std::size_t>(p)] < 0.0)
                m.firstPathCorrect[static_cast<std::size_t>(p)] = elapsedMs;
            if (m.firstAnyCorrect < 0.0)
                m.firstAnyCorrect = elapsedMs;
            if (t.pathCoordinateAuthority(p,c.frequencyHz) > 0.0f
                && m.firstOwnerCorrect < 0.0)
            {
                m.firstOwnerCorrect = elapsedMs;
            }
        }
    }

    const auto resolved=t.resolveContinuousCandidate();
    if (resolved.valid && resolved.candidate.valid
        && isCorrect(resolved.candidate.frequencyHz,target)
        && m.firstResolvedCorrect < 0.0)
    {
        m.firstResolvedCorrect=elapsedMs;
    }

    if (o.measurementAvailable && o.correctionFrequencyHz>0.0f)
    {
        if (m.firstMeasurement < 0.0)
            m.firstMeasurement=elapsedMs;

        if (isCorrect(o.correctionFrequencyHz,target))
        {
            if (m.firstMeasurementCorrect < 0.0)
                m.firstMeasurementCorrect=elapsedMs;
        }
        else if (m.firstMeasurementCorrect < 0.0)
        {
            ++m.wrongMeasurementsBeforeCorrect;
            if (isOctaveFamilyError(o.correctionFrequencyHz,target))
                ++m.octaveMeasurementsBeforeCorrect;
        }
    }

    if (o.valid && o.correctionFrequencyHz>0.0f
        && isCorrect(o.correctionFrequencyHz,target)
        && m.firstValidCorrect < 0.0)
    {
        m.firstValidCorrect=elapsedMs;
    }
}

Metrics runOnset(double target, double snrDb, Kind kind, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(55.0f,1600.0f);
    t.setSensitivity(0.70f);

    Rng rng{seed};
    double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noiseAmp=amp/std::pow(10.0,snrDb/20.0);
    Metrics m;
    ModernPitchEngine::PitchObservation o;

    constexpr int total=static_cast<int>(0.150*sr);
    for(int i=0;i<total;++i)
    {
        phase+=2.0*pi*target/sr;
        if(phase>=2.0*pi) phase-=2.0*pi;

        float x=static_cast<float>(amp)*periodic(kind,phase)
              + static_cast<float>(noiseAmp)*rng.next();

        if(kind==Kind::onsetBurst && i<int(0.003*sr))
            x += static_cast<float>(3.5*amp)*rng.next();

        if(t.processSample(x,o))
            inspectHop(t,o,1000.0*double(i+1)/sr,target,m);
    }
    return m;
}

Metrics runReentry(double target, double snrDb, Kind kind, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(55.0f,1600.0f);
    t.setSensitivity(0.70f);

    Rng rng{seed};
    double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noiseAmp=amp/std::pow(10.0,snrDb/20.0);
    ModernPitchEngine::PitchObservation o;

    const int pre=int(0.060*sr);
    const int silence=int(0.006*sr);
    const int post=int(0.150*sr);

    for(int i=0;i<pre;++i)
    {
        phase+=2*pi*target/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*periodic(kind,phase)+float(noiseAmp)*rng.next();
        static_cast<void>(t.processSample(x,o));
    }
    for(int i=0;i<silence;++i)
        static_cast<void>(t.processSample(0.0f,o));

    Metrics m;
    for(int i=0;i<post;++i)
    {
        phase+=2*pi*target/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*periodic(kind,phase)+float(noiseAmp)*rng.next();
        if(t.processSample(x,o))
            inspectHop(t,o,1000.0*double(i+1)/sr,target,m);
    }
    return m;
}

void print(const char* mode,double hz,double snr,Kind kind,
           std::uint32_t seed,const Metrics& m)
{
    std::cout<<std::fixed<<std::setprecision(5)
             <<"ACQ_PROVENANCE"
             <<" mode="<<mode
             <<" kind="<<kindName(kind)
             <<" hz="<<hz
             <<" snr="<<snr
             <<" seed="<<seed
             <<" full_any="<<m.firstPathAny[0]
             <<" half_any="<<m.firstPathAny[1]
             <<" quarter_any="<<m.firstPathAny[2]
             <<" eighth_any="<<m.firstPathAny[3]
             <<" full_correct="<<m.firstPathCorrect[0]
             <<" half_correct="<<m.firstPathCorrect[1]
             <<" quarter_correct="<<m.firstPathCorrect[2]
             <<" eighth_correct="<<m.firstPathCorrect[3]
             <<" any_correct="<<m.firstAnyCorrect
             <<" owner_correct="<<m.firstOwnerCorrect
             <<" resolved_correct="<<m.firstResolvedCorrect
             <<" measurement_first="<<m.firstMeasurement
             <<" measurement_correct="<<m.firstMeasurementCorrect
             <<" valid_correct="<<m.firstValidCorrect
             <<" wrong_before_correct="<<m.wrongMeasurementsBeforeCorrect
             <<" octave_before_correct="<<m.octaveMeasurementsBeforeCorrect
             <<" hops="<<m.hops
             <<"\n";
}
}

int main()
{
    constexpr std::array<double,6> frequencies{
        82.4069,110.0,220.0,440.0,660.0,880.0
    };
    constexpr std::array<std::uint32_t,2> seeds{
        0x1234567u,0x9e3779b9u
    };

    for(auto seed:seeds)
    {
        for(double hz:frequencies)
        {
            print("onset",hz,6.0,Kind::normal,seed,
                  runOnset(hz,6.0,Kind::normal,seed));
            print("onset",hz,6.0,Kind::strongSecond,seed,
                  runOnset(hz,6.0,Kind::strongSecond,seed));
            print("onset",hz,3.0,Kind::strongSecond,seed,
                  runOnset(hz,3.0,Kind::strongSecond,seed));
            print("onset",hz,6.0,Kind::missingFundamental,seed,
                  runOnset(hz,6.0,Kind::missingFundamental,seed));
        }

        for(double hz:{110.0,220.0,440.0,660.0})
        {
            print("onset",hz,6.0,Kind::onsetBurst,seed,
                  runOnset(hz,6.0,Kind::onsetBurst,seed));
            print("reentry",hz,6.0,Kind::normal,seed,
                  runReentry(hz,6.0,Kind::normal,seed));
            print("reentry",hz,6.0,Kind::strongSecond,seed,
                  runReentry(hz,6.0,Kind::strongSecond,seed));
        }
    }
}
