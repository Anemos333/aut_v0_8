#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteLongDownwardExclusiveProbe.cpp"

namespace
{
double halfAsymmetryNormalized(
    const std::array<double, progressiveMaxSamples>& x,
    int sampleCount,
    int shortLag)
{
    const auto local = measureLocalDoubleLag(x, sampleCount, shortLag, 0.05);
    if (!local.observable || local.bestLongLag <= 0)
        return 0.0;
    const int period = local.bestLongLag;
    const int half = static_cast<int>(std::lround(0.5 * period));
    const int cycles = sampleCount / period;
    if (half < 2 || cycles < 3)
        return 0.0;

    double systematic = 0.0;
    double templateEnergy = 0.0;
    int phases = 0;
    for (int p = 0; p < half && p + half < period; ++p)
    {
        double sumDiff = 0.0;
        double sumLevel2 = 0.0;
        int count = 0;
        for (int k = 0; k < cycles; ++k)
        {
            const int base = k * period;
            if (base + p + half >= sampleCount) break;
            const double a = x[static_cast<std::size_t>(base + p)];
            const double b = x[static_cast<std::size_t>(base + p + half)];
            sumDiff += a - b;
            sumLevel2 += 0.5 * (a * a + b * b);
            ++count;
        }
        if (count < 3) continue;
        const double mean = sumDiff / static_cast<double>(count);
        systematic += mean * mean;
        templateEnergy += sumLevel2 / static_cast<double>(count);
        ++phases;
    }
    if (phases < half / 2 || !(templateEnergy > 1.0e-20))
        return 0.0;
    return systematic / templateEnergy;
}

struct Counts { int correct=0; int high=0; };
constexpr std::array<double,5> normThresholds {0.05,0.07,0.08,0.09,0.10};
constexpr std::array<double,4> ratioCaps {0.80,0.85,0.90,0.95};
}

int main()
{
    std::array<std::array<Counts,ratioCaps.size()>,normThresholds.size()> w5three {};
    std::array<std::array<Counts,ratioCaps.size()>,normThresholds.size()> w8two {};
    int eligibleCorrect=0,eligibleHigh=0;

    for(const auto& seedSet:seedSets)
        for(const auto& profile:profiles)
            for(double f0:frequencies)
                for(double snr:snrs)
                    for(auto seed:seedSet)
                    {
                        const auto x=makeProgressiveVoiceLike(profile,f0,snr,
                            seed ^ static_cast<std::uint32_t>(f0*97.0));
                        const auto e=estimateProgressive(x,1024);
                        if(!e.valid||e.loweredPrimitive||0.5*e.hz<minimumF0) continue;
                        const double ae=std::abs(cents(e.hz,f0));
                        const bool correct=ae<=100.0;
                        const bool high=e.hz>1.5*f0;
                        if(!correct&&!high) continue;
                        if(correct)++eligibleCorrect;
                        if(high)++eligibleHigh;

                        const auto d=measureExclusiveLowerFamily(x,e);
                        const auto local=measureLocalDoubleLag(x,1024,e.lag,0.05);
                        const double norm=halfAsymmetryNormalized(x,1024,e.lag);
                        if(!d.observable||!local.observable) continue;

                        for(std::size_t n=0;n<normThresholds.size();++n)
                            for(std::size_t r=0;r<ratioCaps.size();++r)
                            {
                                const bool common=norm>=normThresholds[n]
                                    && local.ratio<=ratioCaps[r];
                                if(common&&d.witnesses5>=3){if(correct)++w5three[n][r].correct;if(high)++w5three[n][r].high;}
                                if(common&&d.witnesses8>=2){if(correct)++w8two[n][r].correct;if(high)++w8two[n][r].high;}
                            }
                    }

    std::cout<<"DOWNWARD_CONJUNCTION_SUMMARY eligible_correct="<<eligibleCorrect
             <<" eligible_high="<<eligibleHigh;
    for(std::size_t n=0;n<normThresholds.size();++n)
        for(std::size_t r=0;r<ratioCaps.size();++r)
            std::cout<<" n"<<static_cast<int>(normThresholds[n]*100.0+0.5)
                     <<"r"<<static_cast<int>(ratioCaps[r]*100.0+0.5)
                     <<"_w5c="<<w5three[n][r].correct
                     <<" n"<<static_cast<int>(normThresholds[n]*100.0+0.5)
                     <<"r"<<static_cast<int>(ratioCaps[r]*100.0+0.5)
                     <<"_w5h="<<w5three[n][r].high
                     <<" n"<<static_cast<int>(normThresholds[n]*100.0+0.5)
                     <<"r"<<static_cast<int>(ratioCaps[r]*100.0+0.5)
                     <<"_w8c="<<w8two[n][r].correct
                     <<" n"<<static_cast<int>(normThresholds[n]*100.0+0.5)
                     <<"r"<<static_cast<int>(ratioCaps[r]*100.0+0.5)
                     <<"_w8h="<<w8two[n][r].high;
    std::cout<<'\n';

    constexpr std::array<std::uint32_t,8> unresolved{
        1821285621u,1518500249u,2600822924u,1249150122u,
        1065670069u,2614888103u,2438529370u,2240740374u};
    const VoiceProfile* strong=nullptr;
    for(const auto& p:profiles) if(std::string(p.name)=="strong_second") strong=&p;
    if(strong)
        for(auto seed:unresolved)
        {
            constexpr double f0=246.9417;
            const auto x=makeProgressiveVoiceLike(*strong,f0,3.0,
                seed ^ static_cast<std::uint32_t>(f0*97.0));
            const auto e=estimateProgressive(x,1024);
            const auto d=measureExclusiveLowerFamily(x,e);
            const auto local=measureLocalDoubleLag(x,1024,e.lag,0.05);
            const double norm=halfAsymmetryNormalized(x,1024,e.lag);
            std::cout<<std::fixed<<std::setprecision(6)
                     <<"DOWNWARD_CONJUNCTION_EIGHT seed="<<seed
                     <<" norm="<<norm<<" ratio="<<local.ratio
                     <<" w5="<<d.witnesses5<<" w8="<<d.witnesses8
                     <<" strict="<<((norm>=0.08&&local.ratio<=0.90&&d.witnesses5>=3)?1:0)
                     <<'\n';
        }
    return 0;
}
