#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
constexpr int n=480;
constexpr int half=n/2;

struct Rng {
    std::uint32_t s;
    float next() noexcept { s^=s<<13; s^=s>>17; s^=s<<5; return float(s&0xffffu)/32767.5f-1.0f; }
};

float voice(double p) noexcept {
    return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)
               +0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}

double shifted(double hz,double cents) noexcept { return hz*std::exp2(cents/1200.0); }
double centsError(double measured,double target) noexcept { return 1200.0*std::log2(measured/target); }
double wrapPi(double x) noexcept {
    while(x>pi) x-=2*pi;
    while(x<-pi) x+=2*pi;
    return x;
}

template <typename Frame>
std::complex<double> projectHalf(const Frame& frame,int start,double hz) noexcept
{
    double re=0.0,im=0.0;
    for(int i=0;i<half;++i){
        const double w=0.5-0.5*std::cos(2*pi*double(i)/double(half-1));
        const double x=double(frame[size_t(start+i)])*w;
        const double p=2*pi*hz*double(i)/sr;
        re+=x*std::cos(p);
        im-=x*std::sin(p);
    }
    return {re,im};
}

template <typename Frame>
double refine(const Frame& frame,double proposal) noexcept
{
    constexpr double radiusCents=2.0;
    constexpr double dt=double(half)/sr;
    double weightedDelta=0.0,weights=0.0;

    for(int h=1;h<=5;++h){
        const double hf=proposal*double(h);
        if(hf>=0.45*sr) break;
        const auto a=projectHalf(frame,0,hf);
        const auto b=projectHalf(frame,half,hf);
        const double weight=std::sqrt(std::max(0.0,std::abs(a)*std::abs(b)))/std::sqrt(double(h));
        if(!(weight>1e-12)) continue;
        const double observed=std::arg(b)-std::arg(a);
        const double expected=2*pi*hf*dt;
        const double residual=wrapPi(observed-expected);
        double delta=residual/(2*pi*double(h)*dt);
        const double maxDelta=proposal*(std::exp2(radiusCents/1200.0)-1.0);
        delta=std::clamp(delta,-maxDelta,maxDelta);
        weightedDelta+=weight*delta;
        weights+=weight;
    }

    if(!(weights>0.0)) return proposal;
    const double out=proposal+weightedDelta/weights;
    return std::clamp(out,shifted(proposal,-2.0),shifted(proposal,2.0));
}

void run(double target,double snr,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr); t.setRange(55.0f,1600.0f);
    Rng rng{seed}; double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);
    ModernPitchEngine::PitchObservation o;

    for(int i=0;i<n;++i){
        phase+=2*pi*target/sr; if(phase>=2*pi) phase-=2*pi;
        t.processSample(float(amp)*voice(phase)+float(noise)*rng.next(),o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
    (void)t.measureCoordinate(t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,
                              sr,55.0f,1600.0f,n,ws);

    for(double proposalError:{-2.0,2.0}){
        const double proposal=shifted(target,proposalError);

        const auto s0=std::chrono::steady_clock::now();
        const double sourceHz=refine(ws.frame,proposal);
        const auto s1=std::chrono::steady_clock::now();

        const auto r0=std::chrono::steady_clock::now();
        const double residualHz=refine(ws.voiceResidualFrame,proposal);
        const auto r1=std::chrono::steady_clock::now();

        const double se=centsError(sourceHz,target);
        const double re=centsError(residualHz,target);
        const double sus=std::chrono::duration<double,std::micro>(s1-s0).count();
        const double rus=std::chrono::duration<double,std::micro>(r1-r0).count();

        std::cout<<std::fixed<<std::setprecision(6)
                 <<"FAST_SUBFRAME_PHASE hz="<<target<<" snr="<<snr
                 <<" proposal_error="<<proposalError
                 <<" source_error="<<se
                 <<" source_pass="<<(std::abs(se)<=1.5?1:0)
                 <<" source_improves="<<(std::abs(se)<2.0?1:0)
                 <<" source_us="<<sus
                 <<" residual_error="<<re
                 <<" residual_pass="<<(std::abs(re)<=1.5?1:0)
                 <<" residual_improves="<<(std::abs(re)<2.0?1:0)
                 <<" residual_us="<<rus<<"\n";
    }
}
}

int main()
{
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
}
