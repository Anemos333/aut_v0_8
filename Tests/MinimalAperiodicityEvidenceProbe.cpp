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

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
constexpr int n=480;

struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};
enum class Kind{normal,strongSecond,missingFundamental,white,colored,breath};
const char* name(Kind k) noexcept {
 switch(k){case Kind::normal:return "normal";case Kind::strongSecond:return "strong_second";case Kind::missingFundamental:return "missing_fundamental";case Kind::white:return "white";case Kind::colored:return "colored";case Kind::breath:return "breath";}return "unknown";
}
float body(Kind k,double p) noexcept {
 if(k==Kind::strongSecond)return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
 if(k==Kind::missingFundamental)return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}

void run(Kind kind,double f0,double snr,std::uint32_t seed)
{
 ModernPitchEngine::MultiRatePitchTracker t;
 t.prepare(sr);t.setRange(55.0f,1600.0f);t.setSensitivity(0.70f);

 std::array<float,ModernPitchEngine::MultiRatePitchTracker::ringSize> ring{};
 Rng rng{seed};
 const double amp=std::pow(10.0,-24.0/20.0);
 const double noiseAmp=amp/std::pow(10.0,snr/20.0);
 double phase=0.0,lp=0.0,hp=0.0;

 for(int i=0;i<n;++i){
  const double w=rng.next();
  lp+=0.035*(w-lp);
  hp+=0.12*(w-hp);
  const double high=w-hp;
  float x=0.0f;
  if(kind==Kind::white)x=float(amp*w);
  else if(kind==Kind::colored)x=float(amp*2.2*lp);
  else if(kind==Kind::breath)x=float(amp*(0.8*high+0.25*w));
  else{
   phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;
   x=float(amp)*body(kind,phase)+float(noiseAmp)*rng.next();
  }
  ring[size_t(i)]=x;
 }

 ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
 const auto c=t.measureCoordinate(ring,n,n,sr,55.0f,1600.0f,n,ws);

 std::cout<<std::fixed<<std::setprecision(6)
          <<"APERIODIC_EVIDENCE kind="<<name(kind)
          <<" hz="<<f0<<" snr="<<snr<<" seed="<<seed
          <<" valid="<<(c.valid?1:0)
          <<" frequency="<<c.frequencyHz
          <<" confidence="<<c.confidence
          <<" periodicity="<<c.periodicity
          <<"\n";
}
}

int main(){
 constexpr std::array<std::uint32_t,32> seeds{
  0x10001u,0x20003u,0x30007u,0x40009u,0x50015u,0x6001du,0x70025u,0x8002bu,
  0x9002fu,0xa0035u,0xb003bu,0xc003du,0xd0043u,0xe0047u,0xf004du,0x100053u,
  0x110059u,0x120061u,0x130065u,0x14006bu,0x150071u,0x16007fu,0x170083u,0x180089u,
  0x19008fu,0x1a0095u,0x1b009du,0x1c00a3u,0x1d00a7u,0x1e00adu,0x1f00b3u,0x2000b9u
 };
 for(auto seed:seeds){
  for(auto k:{Kind::white,Kind::colored,Kind::breath})run(k,220.0,0.0,seed);
  for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental})
   for(double hz:{110.0,220.0,440.0,660.0})
    for(double snr:{6.0,3.0,0.0})run(k,hz,snr,seed);
 }
}
