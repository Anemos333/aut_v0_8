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

namespace{
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

void run(double f0,double snr,Kind kind,std::uint32_t seed){
 ModernPitchEngine::MultiRatePitchTracker t;t.prepare(sr);t.setRange(55,1600);
 Rng rng{seed};double phase=0;const double amp=std::pow(10.,-24./20.),noise=amp/std::pow(10.,snr/20.);
 ModernPitchEngine::PitchObservation o;
 for(int i=0;i<n;++i){phase+=2*pi*f0/sr;if(phase>=2*pi)phase-=2*pi;float x=float(amp)*body(kind,phase)+float(noise)*rng.next();if(kind==Kind::onsetBurst&&i<int(.003*sr))x+=float(3.5*amp)*rng.next();t.processSample(x,o);}
 ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
 (void)t.measureCoordinate(t.fullRateRing_,t.fullRateWritePosition_,t.fullRateAvailableSamples_,sr,55,1600,n,ws);
 auto corr=[&](int lag){if(lag<=0||lag>=n-8)return-1.f;double c=0,ea=0,eb=0;for(int i=0;i<n-lag;++i){double a=ws.frame[size_t(i)],b=ws.frame[size_t(i+lag)];c+=a*b;ea+=a*a;eb+=b*b;}return float(c/std::sqrt(std::max(1e-20,ea*eb)));};
 auto refine=[&](int tau){int best=tau;float p=corr(tau);for(int d=-2;d<=2;++d){int q=tau+d;if(q<2||q>=n-8)continue;float x=corr(q);if(x>p){p=x;best=q;}}double z=best;if(best>2&&best<n-9){double l=corr(best-1),m=corr(best),r=corr(best+1),den=l-2*m+r;if(std::abs(den)>1e-12)z+=std::clamp(.5*(l-r)/den,-.75,.75);}return sr/z;};
 constexpr std::array<std::array<double,2>,4> bands{{{55,230},{230,460},{460,900},{900,1600}}};
 std::array<double,20> cand{};int count=0;
 for(const auto& b:bands){
  int lo=std::max(2,int(std::floor(sr/b[1]))),hi=std::min(n-16,int(std::ceil(sr/b[0])));if(lo>hi)continue;
  int bt=lo;float bv=ws.difference[size_t(lo)];for(int q=lo+1;q<=hi;++q)if(ws.difference[size_t(q)]<bv){bv=ws.difference[size_t(q)];bt=q;}
  double parent=refine(bt);
  for(int d=1;d<=4;++d){double hz=parent/d;if(hz<55||hz>1600)continue;bool dup=false;for(int j=0;j<count;++j)if(std::abs(cents(cand[size_t(j)],hz))<18)dup=true;if(!dup&&count<int(cand.size()))cand[size_t(count++)]=hz;}
 }
 double best=1e9;for(int i=0;i<count;++i)best=std::min(best,std::abs(cents(cand[size_t(i)],f0)));
 std::cout<<std::fixed<<std::setprecision(6)<<"BANDED_RECALL kind="<<name(kind)<<" hz="<<f0<<" snr="<<snr<<" seed="<<seed<<" count="<<count<<" best_abs_cents="<<best<<" family_recall="<<(best<=85?1:0)<<" fine_recall="<<(best<=1.5?1:0)<<"\n";
}}
int main(){constexpr std::array<double,6>hz{82.4069,110.,220.,440.,660.,880.};constexpr std::array<double,4>snr{12.,6.,3.,0.};constexpr std::array<std::uint32_t,8>seeds{0x1234567u,0x51f15e5du,0x9e3779b9u,0x3141592u,0x2718281u,0xabcdefu,0x13579bu,0x2468acu};for(auto s:seeds)for(auto k:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::onsetBurst})for(double f:hz)for(double n:snr)run(f,n,k,s);}
