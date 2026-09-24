#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExtendedEightProbe.cpp"

namespace
{
constexpr std::array<double,12> sourceFrequencies{110.0,123.4708,146.8324,164.8138,196.0,220.0,246.9417,293.6648,329.6276,440.0,659.2551,880.0};
constexpr std::array<double,3> sourceSnrs{18.0,9.0,3.0};
constexpr std::array<std::uint32_t,8> sourceSeeds{0x0d95748fu,0x728eb658u,0x718bcd58u,0x82154aeeu,0x7b54a41du,0xc25a59b5u,0x9c30d539u,0x2af26013u};
constexpr std::array<int,3> checkpoints{1024,1280,1536};
struct Stats{int cases=0,valid=0,correct=0,high=0,low=0,other=0,exclusive=0,ambiguous=0,ambiguousCorrect=0,ambiguousHigh=0,ambiguousLow=0,ambiguousOther=0;};
}

int main()
{
    F0WholeNoteDetectorV1 detector;detector.prepare(48000.0,55.0,1600.0);
    std::array<Stats,checkpoints.size()>stats{};std::array<float,1536>frame{};
    for(const auto& profile:profiles)for(double f0:sourceFrequencies)for(double snr:sourceSnrs)for(auto seed:sourceSeeds)
    {
        auto mixed=seed^static_cast<std::uint32_t>(f0*97.0);auto x=makePrefixStableExtendedVoiceLike(profile,f0,snr,mixed);
        for(int n=0;n<1536;++n)frame[(std::size_t)n]=(float)x[(std::size_t)n];
        for(std::size_t i=0;i<checkpoints.size();++i)
        {
            auto&s=stats[i];++s.cases;auto a=detector.analyse(frame.data(),checkpoints[i]);if(!a.valid)continue;++s.valid;
            double error=std::abs(1200.0*std::log2(a.hz/f0));bool correct=error<=100.0,high=a.hz>1.5*f0,low=a.hz<0.75*f0;
            if(a.exclusiveLowerWitness)++s.exclusive;
            if(a.ambiguousPrimitive)
            {
                ++s.ambiguous;
                if(correct)++s.ambiguousCorrect;else if(high)++s.ambiguousHigh;else if(low)++s.ambiguousLow;else++s.ambiguousOther;
                continue; // candidate exists internally but is not a new stable publication.
            }
            if(correct)++s.correct;else if(high)++s.high;else if(low)++s.low;else++s.other;
        }
    }

    bool safe=true;
    for(std::size_t i=0;i<checkpoints.size();++i)
    {
        const auto&s=stats[i];
        std::cout<<"SOURCE_DETECTOR_CANDIDATE samples="<<checkpoints[i]<<" ms="<<std::fixed<<std::setprecision(4)<<(1000.0*checkpoints[i]/48000.0)
                 <<" cases="<<s.cases<<" valid="<<s.valid<<" correct="<<s.correct<<" high="<<s.high<<" low="<<s.low<<" other_wrong="<<s.other
                 <<" ambiguous="<<s.ambiguous<<" ambiguous_correct="<<s.ambiguousCorrect<<" ambiguous_high="<<s.ambiguousHigh<<" ambiguous_low="<<s.ambiguousLow<<" ambiguous_other="<<s.ambiguousOther
                 <<" exclusive="<<s.exclusive<<'\n';
        if(s.low!=0||s.ambiguousCorrect!=0||s.ambiguousLow!=0||s.ambiguousOther!=0)safe=false;
    }

    // At 32 ms the frozen source holdout may retain an extreme high candidate
    // only as primitive ambiguity.  It must never be published as high/low.
    const auto& last=stats.back();
    if(last.valid!=1440||last.high!=0||last.low!=0||last.other!=0||last.correct+last.ambiguous!=1440||last.ambiguousHigh!=last.ambiguous)
        safe=false;

    std::cout<<"SOURCE_DETECTOR_CANDIDATE_SAFE="<<(safe?"PASS":"FAIL")<<'\n';
    return 0;
}
