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
#include <vector>

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
double cents(double measured,double target) noexcept { return 1200.0*std::log2(measured/target); }

double pct(std::vector<double> v,double q) {
    if(v.empty()) return -1.0;
    std::sort(v.begin(),v.end());
    const double pos=q*double(v.size()-1);
    const size_t lo=size_t(std::floor(pos)),hi=size_t(std::ceil(pos));
    const double t=pos-double(lo);
    return v[lo]*(1.0-t)+v[hi]*t;
}

void run(double hz,double snr,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr); t.setRange(55.0f,1600.0f); t.setSensitivity(0.70f);
    Rng rng{seed}; double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);
    ModernPitchEngine::PitchObservation o;

    std::array<std::vector<double>,3> errors;
    constexpr std::array<double,3> startsMs{25.0,100.0,200.0};

    constexpr int total=24000;
    for(int i=0;i<total;++i){
        phase+=2*pi*hz/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*voice(phase)+float(noise)*rng.next();
        if(!t.processSample(x,o) || !o.measurementAvailable || !(o.correctionFrequencyHz>0.0f)) continue;
        const double e=std::abs(cents(o.correctionFrequencyHz,hz));
        if(e>100.0) continue;
        const double ms=1000.0*double(i)/sr;
        for(size_t k=0;k<startsMs.size();++k)
            if(ms>=startsMs[k]) errors[k].push_back(e);
    }

    for(size_t k=0;k<startsMs.size();++k){
        const auto& e=errors[k];
        const int pass=int(std::count_if(e.begin(),e.end(),[](double x){return x<=1.5;}));
        std::cout<<std::fixed<<std::setprecision(6)
                 <<"STEADY_F0_PRECISION hz="<<hz<<" snr="<<snr
                 <<" start_ms="<<startsMs[k]
                 <<" samples="<<e.size()
                 <<" pass_1p5_fraction="<<(e.empty()?0.0:double(pass)/double(e.size()))
                 <<" median="<<pct(e,0.50)
                 <<" p95="<<pct(e,0.95)
                 <<" p99="<<pct(e,0.99)
                 <<" worst="<<(e.empty()?-1.0:*std::max_element(e.begin(),e.end()))
                 <<"\n";
    }
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
}
