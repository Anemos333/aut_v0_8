#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteLocalDoubleLagProbe.cpp"

namespace
{
double projectionPower(const std::array<double, progressiveMaxSamples>& x,
                       int sampleCount,
                       double hz)
{
    if (!(hz > 0.0) || hz >= 0.48 * sr)
        return 0.0;
    double re = 0.0, im = 0.0;
    for (int n = 0; n < sampleCount; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(n)
            / static_cast<double>(sampleCount - 1));
        const double p = 2.0 * pi * hz * static_cast<double>(n) / sr;
        const double s = x[static_cast<std::size_t>(n)] * w;
        re += s * std::cos(p);
        im -= s * std::sin(p);
    }
    return re * re + im * im;
}

struct ExclusiveEvidence
{
    bool observable = false;
    int witnesses3 = 0;
    int witnesses5 = 0;
    int witnesses8 = 0;
    int witnesses12 = 0;
    double meanLogRatio = 0.0;
    double minimumLogRatio = 0.0;
    double lowHz = 0.0;
};

ExclusiveEvidence measureExclusiveLowerFamily(
    const std::array<double, progressiveMaxSamples>& x,
    const ProgressiveEstimate& e)
{
    const auto local = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
    if (!local.observable || local.bestLongLag <= 0)
        return {};
    const double lowHz = sr / static_cast<double>(local.bestLongLag);
    if (lowHz < minimumF0)
        return {};

    std::array<double, 4> ratios {};
    int count = 0;
    for (int k : {1, 3, 5, 7})
    {
        const double hz = lowHz * static_cast<double>(k);
        if (hz >= 0.44 * sr)
            continue;
        const double centre = projectionPower(x, 1024, hz);
        std::array<double, 4> side {
            projectionPower(x, 1024, hz - 0.40 * lowHz),
            projectionPower(x, 1024, hz - 0.30 * lowHz),
            projectionPower(x, 1024, hz + 0.30 * lowHz),
            projectionPower(x, 1024, hz + 0.40 * lowHz)
        };
        std::sort(side.begin(), side.end());
        const double localFloor = 0.5 * (side[1] + side[2]);
        ratios[static_cast<std::size_t>(count++)] =
            centre / std::max(1.0e-20, localFloor);
    }
    if (count < 2)
        return {};

    int w3=0,w5=0,w8=0,w12=0;
    double logSum=0.0;
    double minLog=1.0e30;
    for (int i=0;i<count;++i)
    {
        const double r=ratios[static_cast<std::size_t>(i)];
        if (r>=3.0) ++w3;
        if (r>=5.0) ++w5;
        if (r>=8.0) ++w8;
        if (r>=12.0) ++w12;
        const double lr=std::log(std::max(1.0e-20,r));
        logSum+=lr;
        minLog=std::min(minLog,lr);
    }
    return {true,w3,w5,w8,w12,logSum/static_cast<double>(count),minLog,lowHz};
}

struct Counts { int correct=0; int high=0; };
}

int main()
{
    std::array<Counts,4> twoWitness {};
    std::array<Counts,4> threeWitness {};
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
                        if(correct) ++eligibleCorrect;
                        if(high) ++eligibleHigh;
                        const auto d=measureExclusiveLowerFamily(x,e);
                        if(!d.observable) continue;
                        const std::array<int,4> w{d.witnesses3,d.witnesses5,d.witnesses8,d.witnesses12};
                        for(std::size_t i=0;i<w.size();++i)
                        {
                            if(w[i]>=2){if(correct)++twoWitness[i].correct;if(high)++twoWitness[i].high;}
                            if(w[i]>=3){if(correct)++threeWitness[i].correct;if(high)++threeWitness[i].high;}
                        }
                    }

    constexpr std::array<int,4> labels{3,5,8,12};
    std::cout<<"LONG_DOWNWARD_EXCLUSIVE_SUMMARY eligible_correct="<<eligibleCorrect
             <<" eligible_high="<<eligibleHigh;
    for(std::size_t i=0;i<labels.size();++i)
        std::cout<<" t"<<labels[i]<<"_two_correct="<<twoWitness[i].correct
                 <<" t"<<labels[i]<<"_two_high="<<twoWitness[i].high
                 <<" t"<<labels[i]<<"_three_correct="<<threeWitness[i].correct
                 <<" t"<<labels[i]<<"_three_high="<<threeWitness[i].high;
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
            std::cout<<std::fixed<<std::setprecision(6)
                     <<"LONG_DOWNWARD_EXCLUSIVE_EIGHT seed="<<seed
                     <<" low_hz="<<d.lowHz
                     <<" w3="<<d.witnesses3<<" w5="<<d.witnesses5
                     <<" w8="<<d.witnesses8<<" w12="<<d.witnesses12
                     <<" mean_log="<<d.meanLogRatio
                     <<" min_log="<<d.minimumLogRatio<<'\n';
        }
    return 0;
}
