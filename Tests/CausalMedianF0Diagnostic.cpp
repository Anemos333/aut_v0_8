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
    return measured>0.0 ? 1200.0*std::log2(measured/target) : 1.0e9;
}

double median3(double a,double b,double c) noexcept {
    return a+b+c-std::min({a,b,c})-std::max({a,b,c});
}

struct Stats {
    std::vector<double> raw;
    std::vector<double> med3;
    int grossRaw=0;
    int grossMed3=0;
};

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

    std::array<double,3> history{};
    int historyCount=0;
    Stats stats;
    ModernPitchEngine::PitchObservation o;

    constexpr int total=24000;
    for(int i=0;i<total;++i){
        phase+=2*pi*hz/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*voice(phase)+float(noise)*rng.next();
        if(!t.processSample(x,o) || !o.measurementAvailable || !(o.correctionFrequencyHz>0.0f)) continue;

        const double logf=std::log2(double(o.correctionFrequencyHz));
        history[size_t(historyCount%3)]=logf;
        ++historyCount;

        if(i < int(0.100*sr)) continue;

        const double rawErr=std::abs(cents(o.correctionFrequencyHz,hz));
        if(rawErr<=100.0) stats.raw.push_back(rawErr); else ++stats.grossRaw;

        if(historyCount>=3){
            const double m=median3(history[0],history[1],history[2]);
            const double f=std::exp2(m);
            const double e=std::abs(cents(f,hz));
            if(e<=100.0) stats.med3.push_back(e); else ++stats.grossMed3;
        }
    }

    const auto emit=[&](const char* mode,const std::vector<double>& v,int gross){
        const int pass=int(std::count_if(v.begin(),v.end(),[](double x){return x<=1.5;}));
        std::cout<<std::fixed<<std::setprecision(6)
                 <<"CAUSAL_MEDIAN_PRECISION hz="<<hz<<" snr="<<snr
                 <<" mode="<<mode
                 <<" samples="<<v.size()
                 <<" gross="<<gross
                 <<" pass_1p5_fraction="<<(v.empty()?0.0:double(pass)/double(v.size()))
                 <<" median="<<pct(v,0.50)
                 <<" p95="<<pct(v,0.95)
                 <<" p99="<<pct(v,0.99)
                 <<" worst="<<(v.empty()?-1.0:*std::max_element(v.begin(),v.end()))
                 <<"\n";
    };
    emit("raw",stats.raw,stats.grossRaw);
    emit("median3",stats.med3,stats.grossMed3);
}

void motion(double fromHz,double toHz,double snr,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr); t.setRange(55.0f,1600.0f); t.setSensitivity(0.70f);
    Rng rng{seed}; double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);

    std::array<double,3> hist{};
    int hc=0;
    int transitionSample=12000;
    double rawFirst=-1.0,medFirst=-1.0;
    ModernPitchEngine::PitchObservation o;

    for(int i=0;i<24000;++i){
        const double f=i<transitionSample?fromHz:toHz;
        phase+=2*pi*f/sr; if(phase>=2*pi) phase-=2*pi;
        const float x=float(amp)*voice(phase)+float(noise)*rng.next();
        if(!t.processSample(x,o) || !o.measurementAvailable || !(o.correctionFrequencyHz>0.0f)) continue;

        const double lf=std::log2(double(o.correctionFrequencyHz));
        hist[size_t(hc%3)]=lf; ++hc;
        if(i<transitionSample) continue;

        const double rawErr=std::abs(cents(o.correctionFrequencyHz,toHz));
        if(rawFirst<0.0 && rawErr<=1.5)
            rawFirst=1000.0*double(i-transitionSample)/sr;

        if(hc>=3){
            const double mf=std::exp2(median3(hist[0],hist[1],hist[2]));
            const double me=std::abs(cents(mf,toHz));
            if(medFirst<0.0 && me<=1.5)
                medFirst=1000.0*double(i-transitionSample)/sr;
        }
    }

    std::cout<<std::fixed<<std::setprecision(6)
             <<"CAUSAL_MEDIAN_MOTION from="<<fromHz<<" to="<<toHz<<" snr="<<snr
             <<" raw_first_ms="<<rawFirst
             <<" median3_first_ms="<<medFirst
             <<" delta_ms="<<((rawFirst>=0.0&&medFirst>=0.0)?medFirst-rawFirst:-1.0)
             <<"\n";
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz) for(double s:snr) for(auto seed:seeds) run(f,s,seed);
    for(auto seed:seeds){
        motion(220.0,246.94,6.0,seed);
        motion(246.94,196.0,6.0,seed);
        motion(196.0,261.63,6.0,seed);
        motion(440.0,660.0,6.0,seed);
    }
}
