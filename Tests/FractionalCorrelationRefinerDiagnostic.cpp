#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;

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

float fractionalCorrelation(
    const std::array<float,ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& frame,
    int length,
    double lag) noexcept
{
    if(lag<2.0 || lag>=double(length-9)) return -1.0f;
    const int integerLag=int(std::floor(lag));
    const double frac=lag-double(integerLag);
    const int overlap=length-integerLag-1;
    double corr=0.0,ea=0.0,eb=0.0;
    for(int i=0;i<overlap;++i){
        const double a=frame[size_t(i)];
        const double b0=frame[size_t(i+integerLag)];
        const double b1=frame[size_t(i+integerLag+1)];
        const double b=b0+(b1-b0)*frac;
        corr+=a*b; ea+=a*a; eb+=b*b;
    }
    const double den=std::sqrt(std::max(1.0e-20,ea*eb));
    return den>0.0?float(corr/den):-1.0f;
}

double refine(
    const std::array<float,ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& frame,
    int length,
    double effectiveRate,
    double proposal,
    double radiusCents) noexcept
{
    constexpr double step=0.125;
    double bestHz=proposal;
    float best=-2.0f;
    for(double off=-radiusCents;off<=radiusCents+1e-9;off+=step){
        const double hz=shifted(proposal,off);
        const double lag=effectiveRate/hz;
        const float score=fractionalCorrelation(frame,length,lag);
        if(score>best){best=score;bestHz=hz;}
    }
    return bestHz;
}

void run(double hz,double snr,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr); t.setRange(55.0f,1600.0f);
    Rng rng{seed}; double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);
    ModernPitchEngine::PitchObservation o;

    for(int i=0;i<24000;++i){
        phase+=2*pi*hz/sr; if(phase>=2*pi) phase-=2*pi;
        t.processSample(float(amp)*voice(phase)+float(noise)*rng.next(),o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
    int length=256;
    double rate=sr;
    float low=160.0f,high=1600.0f;

    if(hz<=230.0){
        rate=sr/8.0; length=512;
        low=25.0f; high=230.0f;
        (void)t.measureCoordinate(t.eighthRateRing_,t.eighthRateWritePosition_,t.eighthRateAvailableSamples_,
                                  rate,low,high,length,ws);
    } else if(hz<=460.0){
        rate=sr/4.0; length=384;
        low=35.0f; high=460.0f;
        (void)t.measureCoordinate(t.quarterRateRing_,t.quarterRateWritePosition_,t.quarterRateAvailableSamples_,
                                  rate,low,high,length,ws);
    } else if(hz<=900.0){
        rate=sr/2.0; length=ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize;
        low=78.0f; high=900.0f;
        (void)t.measureCoordinate(t.halfRateRing_,t.halfRateWritePosition_,t.halfRateAvailableSamples_,
                                  rate,low,high,length,ws);
    } else {
        (void)t.measureCoordinate(t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,
                                  rate,low,high,length,ws);
    }

    for(double radius:{2.0,5.0}){
        for(double proposalError:{-2.0,2.0}){
            const double proposal=shifted(hz,proposalError);

            const auto b0=std::chrono::steady_clock::now();
            const double sourceHz=refine(ws.frame,length,rate,proposal,radius);
            const auto b1=std::chrono::steady_clock::now();
            const double residualHz=refine(ws.voiceResidualFrame,length,rate,proposal,radius);
            const auto b2=std::chrono::steady_clock::now();

            const double se=centsError(sourceHz,hz);
            const double re=centsError(residualHz,hz);
            const double sus=std::chrono::duration<double,std::micro>(b1-b0).count();
            const double rus=std::chrono::duration<double,std::micro>(b2-b1).count();

            std::cout<<std::fixed<<std::setprecision(6)
                     <<"FRACTIONAL_CORR_SECOND hz="<<hz<<" snr="<<snr
                     <<" radius="<<radius<<" proposal_error="<<proposalError
                     <<" source_error="<<se<<" source_pass="<<(std::abs(se)<=1.5?1:0)
                     <<" source_us="<<sus
                     <<" residual_error="<<re<<" residual_pass="<<(std::abs(re)<=1.5?1:0)
                     <<" residual_us="<<rus<<"\n";
        }
    }
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
}
