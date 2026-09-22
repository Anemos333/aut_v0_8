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
#include <limits>
#include <vector>

namespace {
constexpr double sr=48000.0, pi=3.14159265358979323846; constexpr int n=480;
struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};
enum class Kind{normal,strongSecond,missingFundamental,onsetBurst};
const char* name(Kind k)noexcept{switch(k){case Kind::normal:return"normal";case Kind::strongSecond:return"strong_second";case Kind::missingFundamental:return"missing_fundamental";case Kind::onsetBurst:return"onset_burst";}return"unknown";}
float body(Kind k,double p)noexcept{
 if(k==Kind::strongSecond)return float(.16*std::sin(p)+1.*std::sin(2*p+.17)+.38*std::sin(3*p-.31)+.18*std::sin(4*p+.49)+.09*std::sin(5*p-.63));
 if(k==Kind::missingFundamental)return float(.90*std::sin(2*p+.17)+.58*std::sin(3*p-.31)+.34*std::sin(4*p+.49)+.20*std::sin(5*p-.63)+.12*std::sin(6*p+.27));
 return float(.72*std::sin(p)+.34*std::sin(2*p+.17)+.21*std::sin(3*p-.31)+.13*std::sin(4*p+.49)+.08*std::sin(5*p-.63));
}
double cents(double a,double b){return a>0&&b>0?1200*std::log2(a/b):1e9;}
struct Minimum{int tau=0;float value=1;};

void run(double f0,double snr,Kind kind,std::uint32_t seed){
 ModernPitchEngine::MultiRatePitchTracker t;t.prepare(sr);t.setRange(55,1600);
 Rng rng{seed};double phase=0;const double amp=std::pow(10.,-24./20.),noise=amp/std::pow(10.,snr/20.);
 ModernPitchEngine::PitchObservation o;
 for(int i=0;i<n;++i){phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;float x=float(amp)*body(kind,phase)+float(noise)*rng.next();if(kind==Kind::onsetBurst&&i<int(.003*sr))x+=float(3.5*amp)*rng.next();t.processSample(x,o);}
 ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
 (void)t.measureCoordinate(t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,sr,55,1600,n,ws);

 const int tauMin=std::max(2,int(std::floor(sr/1600.0)));
 const int tauMax=n-16;
 std::vector<Minimum> minima;
 for(int tau=tauMin+1;tau<tauMax;++tau){
   const float v=ws.difference[size_t(tau)];
   if(v<=ws.difference[size_t(tau-1)]&&v<ws.difference[size_t(tau+1)])
     minima.push_back({tau,v});
 }
 std::sort(minima.begin(),minima.end(),[](auto&a,auto&b){return a.value<b.value;});

 for(int k:{1,2,4,8,12,16}){
   double best=1e9;
   const int take=std::min(k,int(minima.size()));
   for(int i=0;i<take;++i){
     const double parent=sr/double(minima[size_t(i)].tau);
     for(int d=1;d<=4;++d){
       const double hz=parent/double(d);
       if(hz<55||hz>1600)continue;
       best=std::min(best,std::abs(cents(hz,f0)));
     }
   }
   std::cout<<std::fixed<<std::setprecision(6)
            <<"LOCAL_MINIMA_RECALL kind="<<name(kind)<<" hz="<<f0<<" snr="<<snr<<" seed="<<seed
            <<" k="<<k<<" minima="<<minima.size()<<" best_abs_cents="<<best
            <<" family_recall="<<(best<=85?1:0)<<" coarse20="<<(best<=20?1:0)<<"\n";
 }
}
}
int main(){
 constexpr std::array<double,6>hz{82.4069,110.,220.,440.,660.,880.};
 constexpr std::array<double,4>snr{12.,6.,3.,0.};
 constexpr std::array<std::uint32_t,16>seeds{0x10001u,0x20003u,0x30007u,0x40009u,0x50015u,0x6001du,0x70025u,0x8002bu,0x9002fu,0xa0035u,0xb003bu,0xc003du,0xd0043u,0xe0047u,0xf004du,0x100053u};
 for(auto s:seeds)for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::onsetBurst})for(double f:hz)for(double q:snr)run(f,q,k,s);
}
