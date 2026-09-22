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

float normal(double p) noexcept {
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49));
}
float strongSecond(double p) noexcept {
 return float(0.18*std::sin(p)+1.0*std::sin(2*p+0.17)+0.35*std::sin(3*p-0.31)+0.15*std::sin(4*p+0.49));
}
double cents(double f,double target) noexcept {return f>0.0?1200.0*std::log2(f/target):1e9;}

void run(double from,double to,bool strong,std::uint32_t seed)
{
 ModernPitchEngine::MultiRatePitchTracker t;
 t.prepare(sr); t.setRange(55.0f,1600.0f); t.setSensitivity(0.70f);
 Rng rng{seed}; double phase=0.0;
 const double amp=std::pow(10.0,-24.0/20.0), noise=amp/std::pow(10.0,6.0/20.0);
 ModernPitchEngine::PitchObservation o;
 auto emit=[&](double hz){
   phase+=2*pi*hz/sr; if(phase>=2*pi)phase-=2*pi;
   const float body=strong?strongSecond(phase):normal(phase);
   return float(amp)*body+float(noise)*rng.next();
 };

 for(int i=0;i<int(0.070*sr);++i) (void)t.processSample(emit(from),o);
 for(int i=0;i<int(0.006*sr);++i) (void)t.processSample(0.0f,o);

 double firstMeasurement=-1,firstCorrect=-1,firstValidCorrect=-1;
 int wrong=0,oldFamily=0,octaveWrong=0;
 for(int i=0;i<int(0.120*sr);++i){
   if(!t.processSample(emit(to),o)) continue;
   const double ms=1000.0*double(i+1)/sr;
   if(o.measurementAvailable&&o.correctionFrequencyHz>0.0f){
     if(firstMeasurement<0)firstMeasurement=ms;
     const double e=std::abs(cents(o.correctionFrequencyHz,to));
     if(e<=1.5){
       if(firstCorrect<0)firstCorrect=ms;
     } else if(firstCorrect<0){
       ++wrong;
       if(std::abs(cents(o.correctionFrequencyHz,from))<=85.0)++oldFamily;
       const double oct=std::log2(double(o.correctionFrequencyHz)/to);
       const int oi=int(std::lround(oct));
       if(oi!=0&&std::abs(oi)<=3&&std::abs(1200.0*(oct-double(oi)))<=85.0)++octaveWrong;
     }
   }
   if(o.valid&&o.correctionFrequencyHz>0.0f&&std::abs(cents(o.correctionFrequencyHz,to))<=1.5&&firstValidCorrect<0)
     firstValidCorrect=ms;
 }
 std::cout<<std::fixed<<std::setprecision(5)
          <<"GAP_NOTE_CHANGE from="<<from<<" to="<<to
          <<" kind="<<(strong?"strong_second":"normal")
          <<" first_measurement="<<firstMeasurement
          <<" first_correct="<<firstCorrect
          <<" first_valid_correct="<<firstValidCorrect
          <<" wrong_before_correct="<<wrong
          <<" old_family_before_correct="<<oldFamily
          <<" octave_before_correct="<<octaveWrong<<"\n";
}
}

int main(){
 constexpr std::array<std::uint32_t,2> seeds{0x1234567u,0x9e3779b9u};
 for(auto seed:seeds)for(bool strong:{false,true}){
  run(110,220,strong,seed); run(220,110,strong,seed);
  run(220,440,strong,seed); run(440,220,strong,seed);
  run(440,660,strong,seed); run(660,440,strong,seed);
 }
}
