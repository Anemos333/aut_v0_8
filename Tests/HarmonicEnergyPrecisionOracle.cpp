#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
constexpr int n=480;

struct Rng {
    std::uint32_t s;
    float next() noexcept { s^=s<<13; s^=s>>17; s^=s<<5; return float(s&0xffffu)/32767.5f-1.0f; }
};

float voice(double p) noexcept {
    return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)
               +0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}

double cents(double measured,double target) { return 1200.0*std::log2(measured/target); }

struct LineBank
{
    const std::array<float,ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& data;
    std::array<double,n> window{};
    double signalEnergy=0.0,windowEnergy=0.0;

    explicit LineBank(
        const std::array<float,ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& d)
        : data(d) {
        for(int i=0;i<n;++i){
            window[size_t(i)]=0.5-0.5*std::cos(2*pi*double(i)/double(n-1));
            const double x=double(data[size_t(i)])*window[size_t(i)];
            signalEnergy+=x*x; windowEnergy+=window[size_t(i)]*window[size_t(i)];
        }
    }

    float line(double hz) const noexcept {
        double re=0.0,im=0.0;
        for(int i=0;i<n;++i){
            const double x=double(data[size_t(i)])*window[size_t(i)];
            const double p=2*pi*hz*double(i)/sr;
            re+=x*std::cos(p); im-=x*std::sin(p);
        }
        const double norm=std::max(1e-20,signalEnergy*windowEnergy);
        return std::clamp(float(std::sqrt(2.0*(re*re+im*im)/norm)),0.0f,1.0f);
    }

    float harmonicEnergy(double f0) const noexcept {
        float sum=0.0f,wSum=0.0f;
        for(int h=1;h<=8;++h){
            const double hz=f0*double(h);
            if(hz>=0.45*sr) break;
            const float w=1.0f/std::sqrt(float(h));
            sum+=w*line(hz); wSum+=w;
        }
        return sum/std::max(1e-6f,wSum);
    }
};

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

    const LineBank source(ws.frame), residual(ws.voiceResidualFrame);

    auto scan=[&](const LineBank& bank){
        double bestHz=target; float best=-1.0f;
        for(int cent=-120;cent<=120;++cent){
            const double hz=target*std::exp2(double(cent)/1200.0);
            const float s=bank.harmonicEnergy(hz);
            if(s>best){best=s;bestHz=hz;}
        }
        return std::pair<double,float>{bestHz,best};
    };

    const auto [srcHz,srcScore]=scan(source);
    const auto [resHz,resScore]=scan(residual);

    std::cout<<std::fixed<<std::setprecision(5)
             <<"HARMONIC_ENERGY_ORACLE hz="<<target<<" snr="<<snr
             <<" source_best="<<srcHz<<" source_cents="<<cents(srcHz,target)
             <<" source_score="<<srcScore
             <<" residual_best="<<resHz<<" residual_cents="<<cents(resHz,target)
             <<" residual_score="<<resScore
             <<" source_pass="<<(std::abs(cents(srcHz,target))<=1.5?1:0)
             <<" residual_pass="<<(std::abs(cents(resHz,target))<=1.5?1:0)
             <<"\n";
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
}
