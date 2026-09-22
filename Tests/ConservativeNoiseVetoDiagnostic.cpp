#include <JuceHeader.h>
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
enum class Kind{normal,strongSecond,missingFundamental,pureSine,onsetBurst,white,colored,breath};
const char* name(Kind k) noexcept{switch(k){case Kind::normal:return"normal";case Kind::strongSecond:return"strong_second";case Kind::missingFundamental:return"missing_fundamental";case Kind::pureSine:return"pure_sine";case Kind::onsetBurst:return"onset_burst";case Kind::white:return"white";case Kind::colored:return"colored";case Kind::breath:return"breath";}return"unknown";}
float body(Kind k,double p) noexcept{
 if(k==Kind::pureSine)return float(std::sin(p));
 if(k==Kind::strongSecond)return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
 if(k==Kind::missingFundamental)return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}
struct F{double low=0,high=0,zcr=0;};
F features(double f0,double snr,Kind k,std::uint32_t seed){
 Rng rng{seed};const double amp=std::pow(10.0,-24.0/20.0),noise=amp/std::pow(10.0,snr/20.0);
 const auto pole=[](double fc){return 1.0-std::exp(-2*pi*fc/sr);};
 const double lc=pole(900),bc=pole(3600);
 double ls=0,bs=0,col=0,bhp=0,prev=0,phase=0,total=0,le=0,he=0;int zc=0;
 for(int i=0;i<n;++i){
  const double w=rng.next();float x=0;
  if(k==Kind::white)x=float(amp*w);
  else if(k==Kind::colored){col+=0.035*(w-col);x=float(2.2*amp*col);}
  else if(k==Kind::breath){bhp+=0.12*(w-bhp);x=float(amp*(0.8*(w-bhp)+0.25*w));}
  else{
   phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;
   x=float(amp)*body(k,phase)+float(noise)*rng.next();
   if(k==Kind::onsetBurst&&i<int(.003*sr))x+=float(3.5*amp)*rng.next();
  }
  if((x>=0)!=(prev>=0)&&std::abs(double(x)-prev)>1e-4)++zc;prev=x;
  ls+=lc*(x-ls);bs+=bc*(x-bs);
  const double hi=x-bs,x2=double(x)*x;total+=x2;le+=ls*ls;he+=hi*hi;
 }
 const double safe=std::max(1e-12,total);
 return {le/safe,he/safe,double(zc)/n};
}
void run(double hz,double snr,Kind k,std::uint32_t seed){
 const auto f=features(hz,snr,k,seed);
 const bool veto=f.low<0.08&&f.high>0.45&&f.zcr>0.45;
 std::cout<<std::fixed<<std::setprecision(6)<<"CONSERVATIVE_NOISE_VETO kind="<<name(k)
          <<" hz="<<hz<<" snr="<<snr<<" seed="<<seed
          <<" low="<<f.low<<" high="<<f.high<<" zcr="<<f.zcr<<" veto="<<(veto?1:0)<<"\n";
}
}
int main(){
 constexpr std::array<double,9> hz{82.4069,110.0,220.0,440.0,660.0,880.0,1100.0,1320.0,1560.0};
 constexpr std::array<double,4> snr{12.0,6.0,3.0,0.0};
 constexpr std::array<std::uint32_t,32> seeds{0x10001u,0x20003u,0x30007u,0x40009u,0x50015u,0x6001du,0x70025u,0x8002bu,0x9002fu,0xa0035u,0xb003bu,0xc003du,0xd0043u,0xe0047u,0xf004du,0x100053u,0x110059u,0x120061u,0x130065u,0x14006bu,0x150071u,0x16007fu,0x170083u,0x180089u,0x19008fu,0x1a0095u,0x1b009du,0x1c00a3u,0x1d00a7u,0x1e00adu,0x1f00b3u,0x2000b9u};
 for(auto seed:seeds){
  for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::pureSine,Kind::onsetBurst})
   for(double f:hz)for(double s:snr)run(f,s,k,seed);
  for(auto k:{Kind::white,Kind::colored,Kind::breath})run(220.0,0.0,k,seed);
 }
}
