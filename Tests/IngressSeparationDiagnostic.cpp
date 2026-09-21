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
#include <string>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int n = 480;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

enum class Kind { tone, strongSecond, missingFundamental, whiteNoise, coloredNoise, hiss };

float signalSample(Kind kind, double phase) noexcept
{
    if (kind == Kind::strongSecond)
        return static_cast<float>(
              0.35 * std::sin(phase)
            + 1.00 * std::sin(2.0 * phase + 0.17)
            + 0.22 * std::sin(3.0 * phase - 0.31)
            + 0.08 * std::sin(4.0 * phase + 0.49));

    if (kind == Kind::missingFundamental)
        return static_cast<float>(
              0.75 * std::sin(2.0 * phase + 0.17)
            + 0.45 * std::sin(3.0 * phase - 0.31)
            + 0.28 * std::sin(4.0 * phase + 0.49)
            + 0.16 * std::sin(5.0 * phase - 0.63));

    return static_cast<float>(
          0.72 * std::sin(phase)
        + 0.34 * std::sin(2.0 * phase + 0.17)
        + 0.21 * std::sin(3.0 * phase - 0.31)
        + 0.13 * std::sin(4.0 * phase + 0.49)
        + 0.08 * std::sin(5.0 * phase - 0.63));
}

const char* kindName(Kind kind)
{
    switch(kind)
    {
        case Kind::tone: return "tone";
        case Kind::strongSecond: return "strong_second";
        case Kind::missingFundamental: return "missing_fundamental";
        case Kind::whiteNoise: return "white_noise";
        case Kind::coloredNoise: return "colored_noise";
        case Kind::hiss: return "hiss";
    }
    return "unknown";
}

void run(Kind kind, double hz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(55.0f,1600.0f);

    Rng rng { seed };
    double phase = 0.0;
    float coloredFast = 0.0f;
    float coloredSlow = 0.0f;
    float previousWhite = 0.0f;
    const double amp = std::pow(10.0,-24.0/20.0);
    const double noiseAmp = amp / std::pow(10.0,snrDb/20.0);

    std::array<float,n> raw {};
    ModernPitchEngine::PitchObservation o;

    for(int i=0;i<n;++i)
    {
        const float white = rng.next();
        float x = 0.0f;

        const bool periodic = kind == Kind::tone
                           || kind == Kind::strongSecond
                           || kind == Kind::missingFundamental;
        if(periodic)
        {
            phase += 2.0*pi*hz/sr;
            if(phase>=2.0*pi) phase-=2.0*pi;
            x = static_cast<float>(amp) * signalSample(kind,phase)
              + static_cast<float>(noiseAmp) * white;
        }
        else if(kind == Kind::whiteNoise)
        {
            x = static_cast<float>(amp) * white;
        }
        else if(kind == Kind::coloredNoise)
        {
            coloredFast = 0.92f*coloredFast + 0.08f*white;
            coloredSlow = 0.992f*coloredSlow + 0.008f*white;
            x = static_cast<float>(amp)
              * (0.52f*white + 0.31f*coloredFast + 0.17f*coloredSlow);
        }
        else
        {
            const float hp = white - 0.92f*previousWhite;
            previousWhite = white;
            x = static_cast<float>(amp) * hp;
        }

        raw[static_cast<std::size_t>(i)] = x;
        t.processSample(x,o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws {};
    static_cast<void>(t.measureCoordinate(
        t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,
        sr,55.0f,1600.0f,n,ws));

    double e=0.0;
    int zc=0;
    for(int i=0;i<n;++i)
    {
        const float x=raw[static_cast<std::size_t>(i)];
        e += static_cast<double>(x)*x;
        if(i>0 && ((x>=0.0f)!=(raw[static_cast<std::size_t>(i-1)]>=0.0f))
           && std::abs(x-raw[static_cast<std::size_t>(i-1)])>1.0e-4f)
            ++zc;
    }
    const double rms=std::sqrt(e/static_cast<double>(n));
    const double zcr=static_cast<double>(zc)/static_cast<double>(n);

    float minCmndf=1.0f;
    for(int tau=2;tau<=n-16;++tau)
        minCmndf=std::min(minCmndf,ws.difference[static_cast<std::size_t>(tau)]);

    const auto maxCorrelation=[&](const auto& frame)
    {
        float best=-1.0f;
        for(int lag=30;lag<=n-16;++lag)
        {
            double corr=0.0,ea=0.0,eb=0.0;
            const int overlap=n-lag;
            for(int i=0;i<overlap;++i)
            {
                const double a=frame[static_cast<std::size_t>(i)];
                const double b=frame[static_cast<std::size_t>(i+lag)];
                corr+=a*b; ea+=a*a; eb+=b*b;
            }
            const double den=std::sqrt(std::max(1.0e-20,ea*eb));
            if(den>0.0) best=std::max(best,static_cast<float>(corr/den));
        }
        return best;
    };

    const float sourceCorr=maxCorrelation(ws.frame);
    const float residualCorr=maxCorrelation(ws.voiceResidualFrame);

    std::cout<<std::fixed<<std::setprecision(5)
             <<"INGRESS_FEATURE"
             <<" kind="<<kindName(kind)
             <<" hz="<<hz
             <<" snr="<<snrDb
             <<" rms="<<rms
             <<" zcr="<<zcr
             <<" min_cmndf="<<minCmndf
             <<" source_corr="<<sourceCorr
             <<" residual_corr="<<residualCorr
             <<"\n";
}
}

int main()
{
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(auto seed:seeds)
    {
        for(double hz:{82.4069,110.0,220.0,440.0,880.0})
            for(double snr:{12.0,6.0,3.0,0.0})
                run(Kind::tone,hz,snr,seed);

        run(Kind::strongSecond,110.0,3.0,seed);
        run(Kind::missingFundamental,110.0,3.0,seed);
        run(Kind::strongSecond,440.0,3.0,seed);
        run(Kind::missingFundamental,440.0,3.0,seed);

        run(Kind::whiteNoise,0.0,0.0,seed);
        run(Kind::coloredNoise,0.0,0.0,seed);
        run(Kind::hiss,0.0,0.0,seed);
    }
}
