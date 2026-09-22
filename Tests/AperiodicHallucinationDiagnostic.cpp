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
#include <string>

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};

enum class Kind{silence,white,colored,breath,burst,weakPeriodic};
const char* name(Kind k) noexcept {
 switch(k){
  case Kind::silence:return "silence";
  case Kind::white:return "white";
  case Kind::colored:return "colored";
  case Kind::breath:return "breath";
  case Kind::burst:return "burst";
  case Kind::weakPeriodic:return "weak_periodic";
 }
 return "unknown";
}
double cents(double f,double t) noexcept {return f>0?1200.0*std::log2(f/t):1e9;}

void run(Kind kind,std::uint32_t seed)
{
 ModernPitchEngine::MultiRatePitchTracker t;
 t.prepare(sr); t.setRange(55.0f,1600.0f); t.setSensitivity(0.70f);
 Rng rng{seed};
 ModernPitchEngine::PitchObservation o;
 const double amp=std::pow(10.0,-24.0/20.0);
 double lp=0.0,hpState=0.0,phase=0.0;
 int hops=0,available=0,valid=0,correct=0,wrong=0,octave=0;
 int firstAvailable=-1,firstValid=-1;
 double minHz=1e9,maxHz=0.0;

 constexpr int total=24000;
 for(int i=0;i<total;++i){
  const double w=rng.next();
  lp += 0.035*(w-lp);
  hpState += 0.12*(w-hpState);
  const double high=w-hpState;
  float x=0.0f;

  if(kind==Kind::white) x=float(amp*w);
  else if(kind==Kind::colored) x=float(amp*2.2*lp);
  else if(kind==Kind::breath) x=float(amp*0.8*high + amp*0.25*w);
  else if(kind==Kind::burst) {
    if(i<int(0.025*sr) || (i>int(0.220*sr)&&i<int(0.235*sr)))
      x=float(amp*3.0*w);
  }
  else if(kind==Kind::weakPeriodic){
    phase+=2.0*pi*220.0/sr;if(phase>=2*pi)phase-=2*pi;
    const double voice=0.72*std::sin(phase)+0.34*std::sin(2*phase+0.17)+0.21*std::sin(3*phase-0.31);
    const double noise=amp/std::pow(10.0,0.0/20.0);
    x=float(0.55*amp*voice + noise*w);
  }

  if(!t.processSample(x,o)) continue;
  ++hops;
  if(o.measurementAvailable && o.correctionFrequencyHz>0.0f){
    ++available;
    if(firstAvailable<0)firstAvailable=i;
    minHz=std::min(minHz,double(o.correctionFrequencyHz));
    maxHz=std::max(maxHz,double(o.correctionFrequencyHz));
    if(kind==Kind::weakPeriodic){
      const double e=std::abs(cents(o.correctionFrequencyHz,220.0));
      if(e<=1.5)++correct; else {
        ++wrong;
        const double oo=std::log2(double(o.correctionFrequencyHz)/220.0);
        const int oi=int(std::lround(oo));
        if(oi!=0&&std::abs(oi)<=3&&std::abs(1200.0*(oo-double(oi)))<=85.0)++octave;
      }
    }
  }
  if(o.valid){
    ++valid;
    if(firstValid<0)firstValid=i;
  }
 }

 std::cout<<std::fixed<<std::setprecision(6)
          <<"APERIODIC_HALLUCINATION kind="<<name(kind)
          <<" seed="<<seed
          <<" hops="<<hops
          <<" available="<<available
          <<" valid="<<valid
          <<" available_fraction="<<(hops?double(available)/hops:0.0)
          <<" valid_fraction="<<(hops?double(valid)/hops:0.0)
          <<" first_available_ms="<<(firstAvailable>=0?1000.0*firstAvailable/sr:-1.0)
          <<" first_valid_ms="<<(firstValid>=0?1000.0*firstValid/sr:-1.0)
          <<" min_hz="<<(available?minHz:-1.0)
          <<" max_hz="<<(available?maxHz:-1.0)
          <<" correct="<<correct
          <<" wrong="<<wrong
          <<" octave="<<octave
          <<"\n";
}
}

int main(){
 constexpr std::array<Kind,6> kinds{Kind::silence,Kind::white,Kind::colored,Kind::breath,Kind::burst,Kind::weakPeriodic};
 constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
 for(auto seed:seeds)for(auto k:kinds)run(k,seed);
}
