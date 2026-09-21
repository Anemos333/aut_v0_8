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

double cents(double measured,double target) noexcept {
    return 1200.0*std::log2(measured/target);
}

double percentile(std::vector<double> v,double q)
{
    if(v.empty()) return -1.0;
    std::sort(v.begin(),v.end());
    const double pos=q*double(v.size()-1);
    const auto lo=size_t(std::floor(pos));
    const auto hi=size_t(std::ceil(pos));
    const double f=pos-double(lo);
    return v[lo]*(1.0-f)+v[hi]*f;
}

void run(double hz,double snr,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr); t.setRange(55.0f,1600.0f); t.setSensitivity(0.70f);

    Rng rng{seed}; double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);

    std::vector<double> sameFamily;
    int measured=0, gross=0, within1p5=0;
    ModernPitchEngine::PitchObservation o;

    constexpr int total=24000;
    for(int i=0;i<total;++i){
        phase+=2*pi*hz/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*voice(phase)+float(noise)*rng.next();
        if(!t.processSample(x,o)) continue;
        if(!o.measurementAvailable || !(o.correctionFrequencyHz>0.0f)) continue;

        ++measured;
        const double e=cents(o.correctionFrequencyHz,hz);
        if(std::abs(e)<=100.0){
            sameFamily.push_back(std::abs(e));
            if(std::abs(e)<=1.5) ++within1p5;
        } else {
            ++gross;
        }
    }

    const double passFrac=sameFamily.empty()?0.0:double(within1p5)/double(sameFamily.size());
    std::cout<<std::fixed<<std::setprecision(6)
             <<"SAME_FAMILY_PRECISION"
             <<" hz="<<hz<<" snr="<<snr
             <<" measured="<<measured
             <<" same_family="<<sameFamily.size()
             <<" gross="<<gross
             <<" pass_1p5_fraction="<<passFrac
             <<" median="<<percentile(sameFamily,0.50)
             <<" p95="<<percentile(sameFamily,0.95)
             <<" p99="<<percentile(sameFamily,0.99)
             <<" worst="<<(sameFamily.empty()?-1.0:*std::max_element(sameFamily.begin(),sameFamily.end()))
             <<"\n";
}
}

int main()
{
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
}
