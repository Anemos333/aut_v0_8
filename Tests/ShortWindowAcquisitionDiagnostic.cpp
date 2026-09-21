#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
struct Rng {
    std::uint32_t s;
    float next() noexcept {
        s^=s<<13; s^=s>>17; s^=s<<5;
        return static_cast<float>(s&0xffffu)/32767.5f-1.0f;
    }
};

double cents(float measured,double target){
    return measured>0.0f
        ? 1200.0*std::log2(static_cast<double>(measured)/target)
        : std::numeric_limits<double>::infinity();
}

float voice(double p){
    return static_cast<float>(
        0.72*std::sin(p)
      + 0.34*std::sin(2*p+0.17)
      + 0.21*std::sin(3*p-0.31)
      + 0.13*std::sin(4*p+0.49)
      + 0.08*std::sin(5*p-0.63));
}

void run(double hz,double snr,std::uint32_t seed){
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(55.0f,1600.0f);
    Rng rng{seed};
    double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noise=amp/std::pow(10.0,snr/20.0);
    ModernPitchEngine::PitchObservation o;

    constexpr int samples=480; // exactly 10 ms at 48 kHz
    for(int i=0;i<samples;++i){
        phase+=2.0*pi*hz/sr;
        if(phase>=2.0*pi) phase-=2.0*pi;
        const float x=static_cast<float>(amp)*voice(phase)
                    + static_cast<float>(noise)*rng.next();
        t.processSample(x,o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
    const auto c=t.measureCoordinate(
        t.fullRateRing_,
        t.fullRateWritePosition_,
        t.fullRateAvailableSamples_,
        sr,
        55.0f,
        1600.0f,
        samples,
        ws);

    const double err=cents(c.frequencyHz,hz);
    std::cout<<std::fixed<<std::setprecision(5)
             <<"SHORT_ACQUIRE"
             <<" hz="<<hz
             <<" snr="<<snr
             <<" measured="<<c.frequencyHz
             <<" cents="<<err
             <<" abs_cents="<<std::abs(err)
             <<" valid="<<(c.valid?1:0)
             <<" confidence="<<c.confidence
             <<" periodicity="<<c.periodicity
             <<" periods_in_10ms="<<(0.010*hz)
             <<" pass_1p5="<<((c.valid&&std::abs(err)<=1.5)?1:0)
             <<"\n";
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,3> snr{12.0,6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    for(double f:hz)
        for(double s:snr)
            for(auto seed:seeds)
                run(f,s,seed);
}
