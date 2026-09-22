#include <JuceHeader.h>
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
enum class Kind{normal,strongSecond,missingFundamental,onsetBurst,white,colored,breath};
const char* name(Kind k) noexcept {
 switch(k){
  case Kind::normal:return "normal";
  case Kind::strongSecond:return "strong_second";
  case Kind::missingFundamental:return "missing_fundamental";
  case Kind::onsetBurst:return "onset_burst";
  case Kind::white:return "white";
  case Kind::colored:return "colored";
  case Kind::breath:return "breath";
 }return "unknown";
}
float body(Kind k,double p) noexcept {
 if(k==Kind::strongSecond)return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
 if(k==Kind::missingFundamental)return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}

struct Features{
 double rms=0,lowRatio=0,midRatio=0,highRatio=0,zcRate=0,fastSlowPeak=0;
};

Features extract(double f0,double snrDb,Kind kind,std::uint32_t seed)
{
 Rng rng{seed};
 const double amp=std::pow(10.0,-24.0/20.0);
 const double noiseAmp=amp/std::pow(10.0,snrDb/20.0);
 const auto onePole=[](double cutoff){return 1.0-std::exp(-2.0*pi*cutoff/sr);};
 const double lowCoef=onePole(900.0),bodyCoef=onePole(3600.0);
 const double fastCoef=1.0-std::exp(-1.0/(0.0020*sr));
 const double slowCoef=1.0-std::exp(-1.0/(0.040*sr));
 double lowState=0,bodyState=0,fast=0,slow=0,previous=0;
 double coloredState=0,breathHpState=0;
 double total=0,lowE=0,midE=0,highE=0,phase=0,peakRatio=0;
 int zc=0;

 for(int i=0;i<n;++i){
  const double w=rng.next();
  float x=0;
  if(kind==Kind::white)x=float(amp*w);
  else if(kind==Kind::colored){
   coloredState+=0.035*(w-coloredState);
   x=float(2.2*amp*coloredState);
  }else if(kind==Kind::breath){
   // same cheap high-frequency emphasis used in the stress diagnostics
   breathHpState+=0.12*(w-breathHpState);
   x=float(amp*(0.8*(w-breathHpState)+0.25*w));
  }else{
   phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;
   x=float(amp)*body(kind,phase)+float(noiseAmp)*rng.next();
   if(kind==Kind::onsetBurst&&i<int(0.003*sr))x+=float(3.5*amp)*rng.next();
  }
  if((x>=0)!=(previous>=0)&&std::abs(double(x)-previous)>1e-4)++zc;
  previous=x;
  lowState+=lowCoef*(x-lowState);
  bodyState+=bodyCoef*(x-bodyState);
  const double lo=lowState,mid=bodyState-lowState,hi=x-bodyState;
  const double x2=double(x)*x;
  total+=x2;lowE+=lo*lo;midE+=mid*mid;highE+=hi*hi;
  fast+=fastCoef*(x2-fast);slow+=slowCoef*(x2-slow);
  peakRatio=std::max(peakRatio,fast/std::max(1e-12,slow));
 }
 Features f;
 f.rms=std::sqrt(total/n);
 const double safe=std::max(1e-12,total);
 f.lowRatio=lowE/safe;f.midRatio=midE/safe;f.highRatio=highE/safe;
 f.zcRate=double(zc)/n;f.fastSlowPeak=peakRatio;
 return f;
}

void run(double hz,double snr,Kind kind,std::uint32_t seed){
 const auto f=extract(hz,snr,kind,seed);
 std::cout<<std::fixed<<std::setprecision(7)
          <<"PREDETECT_FEATURE kind="<<name(kind)
          <<" hz="<<hz<<" snr="<<snr<<" seed="<<seed
          <<" rms="<<f.rms
          <<" low="<<f.lowRatio
          <<" mid="<<f.midRatio
          <<" high="<<f.highRatio
          <<" zcr="<<f.zcRate
          <<" fastslow="<<f.fastSlowPeak<<"\n";
}
}

int main(){
 constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
 constexpr std::array<double,4> snr{12.0,6.0,3.0,0.0};
 constexpr std::array<std::uint32_t,16> seeds{0x10001u,0x20003u,0x30007u,0x40009u,0x50015u,0x6001du,0x70025u,0x8002bu,0x9002fu,0xa0035u,0xb003bu,0xc003du,0xd0043u,0xe0047u,0xf004du,0x100053u};
 for(auto seed:seeds){
  for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::onsetBurst})
   for(double f:hz)for(double s:snr)run(f,s,k,seed);
  for(auto k:{Kind::white,Kind::colored,Kind::breath})run(220.0,0.0,k,seed);
 }
}
