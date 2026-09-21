#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
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
double wrapPi(double x) noexcept { while(x>pi)x-=2*pi; while(x<-pi)x+=2*pi; return x; }

template <typename Frame>
std::complex<double> projectHalf(const Frame& frame,int start,double hz) noexcept
{
    double re=0.0,im=0.0;
    for(int i=0;i<half;++i){
        const double w=0.5-0.5*std::cos(2*pi*double(i)/double(half-1));
        const double x=double(frame[size_t(start+i)])*w;
        const double p=2*pi*hz*double(i)/sr;
        re+=x*std::cos(p); im-=x*std::sin(p);
    }
    return {re,im};
}

struct HarmonicObservation
{
    double deltaHz=0.0;
    double magnitudeProduct=0.0;
    int harmonic=1;
};

template <typename Frame>
std::array<HarmonicObservation,5> observe(const Frame& frame,double proposal) noexcept
{
    std::array<HarmonicObservation,5> out{};
    constexpr double dt=double(half)/sr;
    for(int h=1;h<=5;++h){
        const double hf=proposal*double(h);
        const auto a=projectHalf(frame,0,hf);
        const auto b=projectHalf(frame,half,hf);
        const double observed=std::arg(b)-std::arg(a);
        const double expected=2*pi*hf*dt;
        const double phaseResidual=wrapPi(observed-expected);
        out[size_t(h-1)].deltaHz=phaseResidual/(2*pi*double(h)*dt);
        out[size_t(h-1)].magnitudeProduct=std::max(0.0,std::abs(a)*std::abs(b));
        out[size_t(h-1)].harmonic=h;
    }
    return out;
}

enum class WeightMode { uniform, magnitude, inverseVariance, strongest };
const char* name(WeightMode m) noexcept {
    switch(m){
        case WeightMode::uniform:return "uniform";
        case WeightMode::magnitude:return "magnitude";
        case WeightMode::inverseVariance:return "inverse_variance";
        case WeightMode::strongest:return "strongest";
    }
    return "unknown";
}

double combine(const std::array<HarmonicObservation,5>& obs,
               double proposal,WeightMode mode) noexcept
{
    constexpr double radiusCents=2.0;
    const double maxDelta=proposal*(std::exp2(radiusCents/1200.0)-1.0);

    if(mode==WeightMode::strongest){
        const auto it=std::max_element(obs.begin(),obs.end(),[](const auto& a,const auto& b){
            return a.magnitudeProduct<b.magnitudeProduct;
        });
        const double d=std::clamp(it->deltaHz,-maxDelta,maxDelta);
        return std::clamp(proposal+d,shifted(proposal,-2.0),shifted(proposal,2.0));
    }

    double sum=0.0,wsum=0.0;
    for(const auto& o:obs){
        double w=1.0;
        if(mode==WeightMode::magnitude)
            w=o.magnitudeProduct;
        else if(mode==WeightMode::inverseVariance)
            w=o.magnitudeProduct*double(o.harmonic*o.harmonic);
        if(!(w>1e-20)) continue;
        const double d=std::clamp(o.deltaHz,-maxDelta,maxDelta);
        sum+=w*d; wsum+=w;
    }
    if(!(wsum>0.0)) return proposal;
    return std::clamp(proposal+sum/wsum,shifted(proposal,-2.0),shifted(proposal,2.0));
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

    constexpr std::array<WeightMode,4> modes{
        WeightMode::uniform,WeightMode::magnitude,
        WeightMode::inverseVariance,WeightMode::strongest
    };

    for(double proposalError:{-2.0,2.0}){
        const double proposal=shifted(target,proposalError);
        const auto sourceObs=observe(ws.frame,proposal);
        const auto residualObs=observe(ws.voiceResidualFrame,proposal);

        for(auto mode:modes){
            const double sourceHz=combine(sourceObs,proposal,mode);
            const double residualHz=combine(residualObs,proposal,mode);
            const double se=centsError(sourceHz,target);
            const double re=centsError(residualHz,target);
            std::cout<<std::fixed<<std::setprecision(6)
                     <<"PHASE_WEIGHTING hz="<<target<<" snr="<<snr
                     <<" proposal_error="<<proposalError
                     <<" mode="<<name(mode)
                     <<" source_error="<<se
                     <<" source_pass="<<(std::abs(se)<=1.5?1:0)
                     <<" residual_error="<<re
                     <<" residual_pass="<<(std::abs(re)<=1.5?1:0)
                     <<"\n";
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
