#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr double sr=48000.0,pi=3.14159265358979323846;constexpr int n=480;
struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};
enum class Kind{normal,strongSecond,missingFundamental,onsetBurst};
const char* name(Kind k)noexcept{switch(k){case Kind::normal:return"normal";case Kind::strongSecond:return"strong_second";case Kind::missingFundamental:return"missing_fundamental";case Kind::onsetBurst:return"onset_burst";}return"unknown";}
float body(Kind k,double p)noexcept{
 if(k==Kind::strongSecond)return float(.16*std::sin(p)+1.*std::sin(2*p+.17)+.38*std::sin(3*p-.31)+.18*std::sin(4*p+.49)+.09*std::sin(5*p-.63));
 if(k==Kind::missingFundamental)return float(.90*std::sin(2*p+.17)+.58*std::sin(3*p-.31)+.34*std::sin(4*p+.49)+.20*std::sin(5*p-.63)+.12*std::sin(6*p+.27));
 return float(.72*std::sin(p)+.34*std::sin(2*p+.17)+.21*std::sin(3*p-.31)+.13*std::sin(4*p+.49)+.08*std::sin(5*p-.63));
}
double cents(double a,double b){return a>0&&b>0?1200*std::log2(a/b):1e9;}
struct P{int tau=0;float v=-2;};

void run(double f0,double snr,Kind kind,std::uint32_t seed){
 std::array<float,n>x{};Rng rng{seed};double phase=0,mean=0;
 const double amp=std::pow(10.,-24./20.),noise=amp/std::pow(10.,snr/20.);
 for(int i=0;i<n;++i){phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;float y=float(amp)*body(kind,phase)+float(noise)*rng.next();if(kind==Kind::onsetBurst&&i<int(.003*sr))y+=float(3.5*amp)*rng.next();x[size_t(i)]=y;mean+=y;}
 mean/=n;for(auto&v:x)v-=float(mean);
 std::array<float,n>corr{};corr.fill(-2);
 auto t0=std::chrono::steady_clock::now();
 for(int tau=30;tau<n-16;++tau){double c=0,ea=0,eb=0;for(int i=0;i<n-tau;++i){double a=x[size_t(i)],b=x[size_t(i+tau)];c+=a*b;ea+=a*a;eb+=b*b;}corr[size_t(tau)]=float(c/std::sqrt(std::max(1e-20,ea*eb)));}
 auto t1=std::chrono::steady_clock::now();
 const double scanUs=std::chrono::duration<double,std::micro>(t1-t0).count();
 constexpr std::array<std::array<double,2>,4>bands{{{55,230},{230,460},{460,900},{900,1600}}};
 for(int perBand:{1,2,3,4,6}){
  double best=1e9;int candidates=0;
  for(const auto&b:bands){
   int lo=std::max(30,int(std::floor(sr/b[1]))),hi=std::min(n-16,int(std::ceil(sr/b[0])));
   std::vector<P> peaks;
   for(int tau=std::max(lo+1,31);tau<std::min(hi,n-16);++tau)
    if(corr[size_t(tau)]>=corr[size_t(tau-1)]&&corr[size_t(tau)]>corr[size_t(tau+1)])peaks.push_back({tau,corr[size_t(tau)]});
   if(peaks.empty()){int bt=lo;float bv=corr[size_t(lo)];for(int q=lo+1;q<=hi;++q)if(corr[size_t(q)]>bv){bv=corr[size_t(q)];bt=q;}peaks.push_back({bt,bv});}
   std::sort(peaks.begin(),peaks.end(),[](auto&a,auto&b){return a.v>b.v;});
   int take=std::min(perBand,int(peaks.size()));
   for(int i=0;i<take;++i){
    double parent=sr/double(peaks[size_t(i)].tau);
    for(int d=1;d<=4;++d){double hz=parent/d;if(hz<55||hz>1600)continue;++candidates;best=std::min(best,std::abs(cents(hz,f0)));}
   }
  }
  std::cout<<std::fixed<<std::setprecision(6)<<"AUTOCORR_RECALL kind="<<name(kind)<<" hz="<<f0<<" snr="<<snr<<" seed="<<seed
           <<" per_band="<<perBand<<" candidates="<<candidates<<" scan_us="<<scanUs<<" best_abs_cents="<<best
           <<" family="<<(best<=85?1:0)<<" coarse20="<<(best<=20?1:0)<<"\n";
 }
}
}
int main(){
 constexpr std::array<double,6>hz{82.4069,110.,220.,440.,660.,880.};
 constexpr std::array<double,4>snr{12.,6.,3.,0.};
 constexpr std::array<std::uint32_t,8>seeds{0x1234567u,0x51f15e5du,0x9e3779b9u,0x3141592u,0x2718281u,0xabcdefu,0x13579bu,0x2468acu};
 for(auto s:seeds)for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::onsetBurst})for(double f:hz)for(double q:snr)run(f,q,k,s);
}
