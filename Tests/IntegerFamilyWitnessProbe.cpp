#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <chrono>
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
enum class Kind{normal,strongSecond,missingFundamental};
const char* name(Kind k) noexcept{switch(k){case Kind::normal:return"normal";case Kind::strongSecond:return"strong_second";case Kind::missingFundamental:return"missing_fundamental";}return"unknown";}
float body(Kind k,double p) noexcept{
 if(k==Kind::strongSecond)return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
 if(k==Kind::missingFundamental)return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}
struct Frame{
 std::array<float,n>x{};
 Frame(double f,double snr,Kind k,std::uint32_t seed){
  Rng rng{seed};const double amp=std::pow(10.0,-24.0/20.0),noise=amp/std::pow(10.0,snr/20.0);
  double phase=0,mean=0;
  for(int i=0;i<n;++i){phase+=2*pi*f/sr;if(phase>=2*pi)phase-=2*pi;x[size_t(i)]=float(amp)*body(k,phase)+float(noise)*rng.next();mean+=x[size_t(i)];}
  mean/=n;for(auto&v:x)v-=float(mean);
 }
 float corr(int lag)const noexcept{
  if(lag<=0||lag>=n-8)return -2;
  double c=0,ea=0,eb=0;const int overlap=n-lag;
  for(int i=0;i<overlap;++i){const double a=x[size_t(i)],b=x[size_t(i+lag)];c+=a*b;ea+=a*a;eb+=b*b;}
  const double den=std::sqrt(std::max(1e-20,ea*eb));return den>0?float(c/den):-2;
 }
};
void run(double f0,double snr,Kind kind,int alias,std::uint32_t seed){
 Frame f(f0,snr,kind,seed);
 const double candidate=f0*alias;
 const int tau=int(std::lround(sr/candidate));
 std::array<float,4> c{{-2,-2,-2,-2}};
 const auto t0=std::chrono::steady_clock::now();
 for(int d=1;d<=4;++d)c[size_t(d-1)]=f.corr(d*tau);
 const auto t1=std::chrono::steady_clock::now();
 int best=0;float bestScore=-3;
 for(int d=1;d<=4;++d)if(c[size_t(d-1)]>bestScore){bestScore=c[size_t(d-1)];best=d;}
 const bool available=c[size_t(alias-1)]>-1.5f;
 std::cout<<std::fixed<<std::setprecision(6)
          <<"INTEGER_FAMILY_WITNESS kind="<<name(kind)<<" hz="<<f0<<" snr="<<snr<<" alias="<<alias<<" seed="<<seed
          <<" c1="<<c[0]<<" c2="<<c[1]<<" c3="<<c[2]<<" c4="<<c[3]
          <<" best_divisor="<<best<<" correct="<<(available&&best==alias?1:0)
          <<" available="<<(available?1:0)
          <<" us="<<std::chrono::duration<double,std::micro>(t1-t0).count()<<"\n";
}
}
int main(){
 constexpr std::array<double,5> hz{110.0,220.0,440.0,660.0,880.0};
 constexpr std::array<double,3> snr{6.0,3.0,0.0};
 constexpr std::array<std::uint32_t,16> seeds{0x10001u,0x20003u,0x30007u,0x40009u,0x50015u,0x6001du,0x70025u,0x8002bu,0x9002fu,0xa0035u,0xb003bu,0xc003du,0xd0043u,0xe0047u,0xf004du,0x100053u};
 for(auto seed:seeds)for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental})for(double f:hz)for(double s:snr)for(int a=1;a<=4;++a)run(f,s,k,a,seed);
}
