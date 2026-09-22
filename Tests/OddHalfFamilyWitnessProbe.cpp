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
constexpr double twoPi=2.0*pi;
constexpr int n=480;

struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};
enum class Kind{normal,strongSecond,missingFundamental,white};
const char* name(Kind k) noexcept {switch(k){case Kind::normal:return "normal";case Kind::strongSecond:return "strong_second";case Kind::missingFundamental:return "missing_fundamental";case Kind::white:return "white";}return "unknown";}
float body(Kind k,double p) noexcept {
 if(k==Kind::strongSecond)return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
 if(k==Kind::missingFundamental)return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
 return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}
float clamp01(float x) noexcept{return std::clamp(x,0.0f,1.0f);}

struct Frame{
 std::array<float,n> source{},residual{};
 std::array<double,n> hann{};
 double se=0,re=0,we=0;
 Frame(double f0,double snr,Kind kind,std::uint32_t seed){
  Rng rng{seed};const double amp=std::pow(10.0,-24.0/20.0);const double noise=amp/std::pow(10.0,snr/20.0);
  double phase=0,mean=0;
  for(int i=0;i<n;++i){
   const double w=rng.next();
   float x;
   if(kind==Kind::white)x=float(amp*w);
   else{phase+=twoPi*f0/sr;if(phase>=twoPi)phase-=twoPi;x=float(amp)*body(kind,phase)+float(noise)*rng.next();}
   source[size_t(i)]=x;mean+=x;
  }
  mean/=n;for(auto& x:source)x-=float(mean);
  double num=0,den=0;
  for(int i=1;i<n;++i){const double a=source[size_t(i)],b=source[size_t(i-1)];num+=a*b;den+=b*b;}
  const float p=float(std::clamp(num/std::max(1e-20,den),-0.92,0.92));
  residual[0]=source[0];for(int i=1;i<n;++i)residual[size_t(i)]=source[size_t(i)]-p*source[size_t(i-1)];
  for(int i=0;i<n;++i){const double w=0.5-0.5*std::cos(twoPi*double(i)/double(n-1));hann[size_t(i)]=w;we+=w*w;const double a=source[size_t(i)]*w,b=residual[size_t(i)]*w;se+=a*a;re+=b*b;}
 }
 float line(bool useResidual,double hz) const noexcept {
  const auto& x=useResidual?residual:source;const double e=useResidual?re:se;
  if(!(hz>0)||hz>=0.48*sr)return 0;
  double rr=0,ii=0;
  for(int i=0;i<n;++i){const double y=x[size_t(i)]*hann[size_t(i)],ph=twoPi*hz*double(i)/sr;rr+=y*std::cos(ph);ii-=y*std::sin(ph);}
  return clamp01(float(std::sqrt(2*(rr*rr+ii*ii)/std::max(1e-20,e*we))));
 }
 float oddHalf(bool residualMode,double candidate) const noexcept {
  return 0.5f*(line(residualMode,1.5*candidate)+line(residualMode,2.5*candidate));
 }
};

void run(double f0,double snr,Kind kind,std::uint32_t seed){
 Frame f(f0,snr,kind,seed);
 for(bool residual:{false,true}){
  const auto t0=std::chrono::steady_clock::now();
  const float atTrue=f.oddHalf(residual,f0);
  const float atDouble=f.oddHalf(residual,2*f0);
  const auto t1=std::chrono::steady_clock::now();
  std::cout<<std::fixed<<std::setprecision(6)
           <<"ODD_HALF_WITNESS mode="<<(residual?"residual":"source")
           <<" kind="<<name(kind)<<" hz="<<f0<<" snr="<<snr<<" seed="<<seed
           <<" true_candidate="<<atTrue
           <<" double_candidate="<<atDouble
           <<" alias_margin="<<(atDouble-atTrue)
           <<" alias_detected="<<(atDouble>atTrue?1:0)
           <<" us="<<std::chrono::duration<double,std::micro>(t1-t0).count()<<"\n";
 }
}
}

int main(){
 constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
 constexpr std::array<double,3> snr{6.0,3.0,0.0};
 constexpr std::array<std::uint32_t,8> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u,0x3141592u,0x2718281u,0xabcdefu,0x13579bu,0x2468acu};
 for(auto seed:seeds){
  for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental})for(double f:hz)for(double s:snr)run(f,s,k,seed);
  run(220.0,0.0,Kind::white,seed);
 }
}
