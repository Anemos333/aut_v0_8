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
struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};
double cents(double f,double t) noexcept{return f>0?1200.0*std::log2(f/t):1e9;}
float voice(double p) noexcept{return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49));}

void run(double levelDb,std::uint32_t seed)
{
 ModernPitchEngine::MultiRatePitchTracker t;
 t.prepare(sr);t.setRange(55.0f,1600.0f);t.setSensitivity(0.70f);
 Rng rng{seed};double phase=0;
 const double amp=std::pow(10.0,levelDb/20.0);
 const double noise=amp/std::pow(10.0,6.0/20.0);
 ModernPitchEngine::PitchObservation o;
 int hops=0,available=0,valid=0,correct=0;
 double first=-1;
 for(int i=0;i<24000;++i){
  phase+=2*pi*220.0/sr;if(phase>=2*pi)phase-=2*pi;
  const float x=float(amp)*voice(phase)+float(noise)*rng.next();
  if(!t.processSample(x,o))continue;
  ++hops;
  if(o.measurementAvailable&&o.correctionFrequencyHz>0){
   ++available;
   if(std::abs(cents(o.correctionFrequencyHz,220.0))<=1.5){
    ++correct;if(first<0)first=1000.0*double(i+1)/sr;
   }
  }
  if(o.valid)++valid;
 }
 std::cout<<std::fixed<<std::setprecision(6)
          <<"RMS_GATE_PROBE level_db="<<levelDb<<" seed="<<seed
          <<" hops="<<hops<<" available="<<available<<" valid="<<valid
          <<" correct="<<correct<<" first_correct_ms="<<first<<"\n";
}
}

int main(){
 constexpr std::array<double,7> levels{-42.0,-50.0,-54.0,-57.0,-58.0,-60.0,-66.0};
 constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
 for(auto seed:seeds)for(double d:levels)run(d,seed);
}
