#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0AmbiguousTransitionBoundProbe.cpp"

namespace
{
struct PublicationState
{
    bool hasStable = false;
    double stableHz = 0.0;
    bool transition = false;

    static double centsDistance(double a, double b) noexcept
    {
        if (!(a > 0.0) || !(b > 0.0)) return 1.0e9;
        return std::abs(1200.0 * std::log2(a / b));
    }

    void seedStable(double hz) noexcept
    {
        hasStable = hz > 0.0;
        stableHz = hasStable ? hz : 0.0;
        transition = false;
    }

    void apply(const F0WholeNoteDetectorV1::Analysis& a) noexcept
    {
        if (!a.valid)
        {
            transition = true;
            return;
        }

        if (a.ambiguousPrimitive)
        {
            // History controls publication/state only.  It never proves that
            // the current physical family is the previous one.
            if (hasStable && centsDistance(a.hz, stableHz) <= 100.0)
            {
                transition = false; // retain the already-published stable F0
                return;
            }
            transition = true;      // do not publish the ambiguous candidate
            return;
        }

        stableHz = a.hz;
        hasStable = true;
        transition = false;
    }
};

std::array<float,1536> toFloatFrame(const ExtendedSignal& x)
{
    std::array<float,1536> f{};
    for(int i=0;i<1536;++i)f[(std::size_t)i]=(float)x[(std::size_t)i];
    return f;
}

ExtendedSignal fixedWindow(const LongSignal& x,int end)
{
    ExtendedSignal y{};
    const int start=end-1536;
    for(int n=0;n<1536;++n)y[(std::size_t)n]=x[(std::size_t)(start+n)];
    return y;
}

template<std::size_t N>
void scanCorpus(const char* name,const std::array<std::uint32_t,N>& seeds)
{
    constexpr std::array<double,12> freqs{110.0,123.4708,146.8324,164.8138,196.0,220.0,246.9417,293.6648,329.6276,440.0,659.2551,880.0};
    constexpr std::array<double,3> snrs{18.0,9.0,3.0};
    F0WholeNoteDetectorV1 detector;detector.prepare(48000.0,55.0,1600.0);
    int cases=0,falseTransition=0,trueHighTransition=0,publishedHigh=0,publishedLow=0;
    for(const auto& profile:profiles)for(double f0:freqs)for(double snr:snrs)for(auto seed:seeds)
    {
        const auto mixed=seed^static_cast<std::uint32_t>(f0*97.0);
        const auto x=makePrefixStableExtendedVoiceLike(profile,f0,snr,mixed);
        const auto frame=toFloatFrame(x);
        const auto a=detector.analyse(frame.data(),1536);
        PublicationState state;state.seedStable(f0);state.apply(a);++cases;
        const bool candidateCorrect=a.valid&&PublicationState::centsDistance(a.hz,f0)<=100.0;
        const bool candidateHigh=a.valid&&a.hz>1.5*f0;
        const bool candidateLow=a.valid&&a.hz<0.75*f0;
        if(state.transition&&candidateCorrect)++falseTransition;
        if(state.transition&&candidateHigh)++trueHighTransition;
        if(!state.transition&&candidateHigh)++publishedHigh;
        if(!state.transition&&candidateLow)++publishedLow;
    }
    std::cout<<"STATEFUL_CORPUS set="<<name<<" cases="<<cases<<" false_transition="<<falseTransition<<" high_transition="<<trueHighTransition<<" published_high="<<publishedHigh<<" published_low="<<publishedLow<<'\n';
}

template<std::size_t N>
void scanHardCases(const char* name,const std::array<std::uint32_t,N>& seeds)
{
    constexpr double truth=246.9417,snr=3.0;
    constexpr std::array<int,5> ends{1536,1920,2304,2688,3072};
    const auto& profile=strongSecondProfile();
    F0WholeNoteDetectorV1 detector;detector.prepare(48000.0,55.0,1600.0);
    int tracked=0,resolved40=0,resolved48=0,resolved56=0,resolved64=0,unresolved64=0;
    for(auto seed:seeds)
    {
        const auto mixed=seed^static_cast<std::uint32_t>(truth*97.0);
        const auto x=makeLongPrefixStableVoiceLike(profile,truth,snr,mixed);
        PublicationState state;state.seedStable(truth);
        bool isTracked=false,resolved=false;int resolvedAt=0;
        for(int end:ends)
        {
            const auto w=fixedWindow(x,end);const auto f=toFloatFrame(w);const auto a=detector.analyse(f.data(),1536);
            if(end==1536&&a.valid&&a.ambiguousPrimitive&&a.hz>1.5*truth){isTracked=true;++tracked;}
            if(!isTracked)continue;
            state.apply(a);
            std::cout<<std::fixed<<std::setprecision(4)<<"STATEFUL_EVOLUTION set="<<name<<" seed="<<seed<<" end_ms="<<(1000.0*end/48000.0)<<" hz="<<a.hz<<" ambiguous="<<(a.ambiguousPrimitive?1:0)<<" transition="<<(state.transition?1:0)<<" stable_hz="<<state.stableHz<<'\n';
            if(!resolved&&!state.transition&&PublicationState::centsDistance(state.stableHz,truth)<=100.0){resolved=true;resolvedAt=end;}
        }
        if(isTracked){if(!resolved)++unresolved64;else if(resolvedAt<=1920)++resolved40;else if(resolvedAt<=2304)++resolved48;else if(resolvedAt<=2688)++resolved56;else ++resolved64;}
    }
    std::cout<<"STATEFUL_BOUND set="<<name<<" tracked="<<tracked<<" resolved_by_40="<<resolved40<<" resolved_by_48="<<resolved48<<" resolved_by_56="<<resolved56<<" resolved_by_64="<<resolved64<<" unresolved_at_64="<<unresolved64<<'\n';
}
}

int main()
{
    // Unit semantics: ambiguity coherent with an existing stable must not create
    // a false transition; the same ambiguity with no stable must acquire.
    PublicationState s;s.seedStable(440.0);
    F0WholeNoteDetectorV1::Analysis a{};a.valid=true;a.hz=442.0;a.ambiguousPrimitive=true;s.apply(a);
    const bool coherentKeepsStable=!s.transition&&PublicationState::centsDistance(s.stableHz,440.0)<1.0;
    PublicationState empty;empty.apply(a);const bool noStableAcquires=empty.transition&&!empty.hasStable;
    a.hz=880.0;s.apply(a);const bool octaveConflictTransitions=s.transition;
    std::cout<<"STATEFUL_UNIT coherent_keeps_stable="<<(coherentKeepsStable?1:0)<<" no_stable_acquires="<<(noStableAcquires?1:0)<<" octave_conflict_transitions="<<(octaveConflictTransitions?1:0)<<'\n';

    constexpr std::array<std::uint32_t,8>A{0x0d95748fu,0x728eb658u,0x718bcd58u,0x82154aeeu,0x7b54a41du,0xc25a59b5u,0x9c30d539u,0x2af26013u};
    constexpr std::array<std::uint32_t,8>B{0xa4093822u,0x299f31d0u,0x082efa98u,0xec4e6c89u,0x452821e6u,0x38d01377u,0xbe5466cfu,0x34e90c6cu};
    constexpr std::array<std::uint32_t,8>C{0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u,0xc1059ed8u,0x367cd507u};
    scanCorpus("a",A);scanCorpus("b",B);scanCorpus("c",C);
    scanHardCases("a",A);scanHardCases("b",B);scanHardCases("c",C);
    return 0;
}
