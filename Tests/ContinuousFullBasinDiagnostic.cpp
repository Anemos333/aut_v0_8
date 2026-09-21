#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
struct Rng { std::uint32_t s; float next(){s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;} };

void run(double hz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(45.0f,1600.0f);
    Rng rng{seed};
    double phase=0.0;
    float nf=0.0f, ns=0.0f;
    const double voiceAmp=std::pow(10.0,-42.0/20.0);
    const double noiseAmp=voiceAmp/std::pow(10.0,snrDb/20.0);
    ModernPitchEngine::PitchObservation o;
    for(int i=0;i<24000;++i){
        phase += 2.0*pi*hz/sr; if(phase>=2.0*pi) phase-=2.0*pi;
        double v=std::sin(phase)+0.44*std::sin(2*phase+0.17)+0.23*std::sin(3*phase+0.41)+0.12*std::sin(4*phase+0.73);
        const float w=rng.next(); nf=0.92f*nf+0.08f*w; ns=0.992f*ns+0.008f*w;
        const float x=float(voiceAmp*v + noiseAmp*(0.52*w+0.31*nf+0.17*ns));
        t.processSample(x,o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
    auto c=t.measureCoordinate(t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,
                               sr,std::max(160.0f,t.minimumPitchHz_),std::min(t.maximumPitchHz_,2600.0f),
                               ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,ws);
    const int n=ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize;
    const int tauMin=std::clamp(int(std::floor(sr/std::min(t.maximumPitchHz_,2600.0f))),2,n-16);
    const int tauMax=std::clamp(int(std::ceil(sr/std::max(160.0f,t.minimumPitchHz_))),tauMin+1,n-16);
    const float threshold=0.12f+0.16f*t.sensitivity_;
    int first=-1, global=tauMin; float gv=ws.difference[std::size_t(tauMin)];
    for(int tau=tauMin;tau<=tauMax;++tau){
        const float v=ws.difference[std::size_t(tau)];
        if(v<gv){gv=v;global=tau;}
        if(first<0&&v<threshold){int local=tau;while(local+1<=tauMax&&ws.difference[std::size_t(local+1)]<ws.difference[std::size_t(local)])++local;first=local;}
    }
    auto thz=[&](int tau){return tau>0?sr/double(tau):-1.0;};
    std::cout<<"FULL_BASIN target="<<hz<<" snr="<<snrDb
             <<" measured="<<c.frequencyHz
             <<" first_tau="<<first<<" first_hz="<<thz(first)
             <<" global_tau="<<global<<" global_hz="<<thz(global)
             <<" global_yin="<<gv<<"\n";
}
}
int main(){
 for(double hz:{440.0,520.0,660.0,880.0,1100.0,1320.0,1560.0})
   for(double snr:{12.0,6.0,3.0}) run(hz,snr,0x51f15e5du);
}
