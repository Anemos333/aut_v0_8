#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExtendedEightProbe.cpp"

namespace {
constexpr double kSr=48000.0;

struct AcfEvidence {
    bool observable=false;
    double shortPeak=0,longPeak=0;
    double shortProm=0,longProm=0;
    double promRatio=0,peakDelta=0;
    int shortLag=0,longLag=0;
};

double median(std::vector<double> v){
    if(v.empty()) return 0.0;
    auto mid=v.begin()+static_cast<std::ptrdiff_t>(v.size()/2);
    std::nth_element(v.begin(),mid,v.end()); double m=*mid;
    if((v.size()&1u)==0u){auto lo=std::max_element(v.begin(),mid);m=.5*(m+*lo);} return m;
}

double corrCommon(const float* x,int overlap,int lag,double mean){
    double ab=0,aa=0,bb=0;
    for(int i=0;i<overlap;++i){double a=(double)x[i]-mean,b=(double)x[i+lag]-mean;ab+=a*b;aa+=a*a;bb+=b*b;}
    return ab/std::sqrt(std::max(1e-30,aa*bb));
}

AcfEvidence measureAcf(const float* x,int n,int shortLag){
    if(!x||n<256||shortLag<2) return {};
    const int longCentre=2*shortLag;
    const int longRad=std::max(2,(int)std::ceil(.05*longCentre));
    const int longHi=std::min(n-40,longCentre+longRad);
    if(longHi<=longCentre-longRad||n-longHi<40) return {};
    const int overlap=n-longHi;
    double mean=0;for(int i=0;i<n;++i)mean+=x[i];mean/=n;

    auto local=[&](int centre){
        const int rad=std::max(2,(int)std::ceil(.05*centre));
        const int lo=std::max(2,centre-rad),hi=std::min(n-overlap,centre+rad);
        double peak=-2;int peakLag=centre;
        for(int lag=lo;lag<=hi;++lag){double c=corrCommon(x,overlap,lag,mean);if(c>peak){peak=c;peakLag=lag;}}
        const int floorRad=std::max(10,(int)std::ceil(.20*centre));
        const int flo=std::max(2,centre-floorRad),fhi=std::min(n-overlap,centre+floorRad);
        std::vector<double> floor; floor.reserve((std::size_t)std::max(0,fhi-flo+1));
        for(int lag=flo;lag<=fhi;++lag) if(lag<lo||lag>hi) floor.push_back(corrCommon(x,overlap,lag,mean));
        const double base=median(std::move(floor));
        return std::array<double,3>{peak,peak-base,(double)peakLag};
    };

    auto s=local(shortLag),l=local(longCentre);
    AcfEvidence e; e.observable=true;e.shortPeak=s[0];e.longPeak=l[0];e.shortProm=s[1];e.longProm=l[1];
    e.promRatio=l[1]/std::max(1e-9,std::abs(s[1]));e.peakDelta=l[0]-s[0];e.shortLag=(int)std::lround(s[2]);e.longLag=(int)std::lround(l[2]);return e;
}

struct Rule{double ratio,delta;};
constexpr std::array<Rule,12> rules{{
 {1.05,.00},{1.10,.00},{1.20,.00},{1.35,.00},
 {1.05,.02},{1.10,.02},{1.20,.02},{1.35,.02},
 {1.05,.05},{1.10,.05},{1.20,.05},{1.35,.05}
}};
struct Stats{int frames=0,amb=0,ambCorrect=0,ambHigh=0,ambOther=0;std::array<int,rules.size()>votes{},fixed{},falseLower{},wrongLower{};std::vector<double>us;};

template<std::size_t N> void runSet(const char*phase,const char*name,const std::array<std::uint32_t,N>&seeds){
 constexpr std::array<double,12>freqs{110,123.4708,146.8324,164.8138,196,220,246.9417,293.6648,329.6276,440,659.2551,880};
 constexpr std::array<double,3>snrs{18,9,3}; constexpr std::array<int,3>cps{1024,1280,1536};
 F0WholeNoteDetectorV1 d;d.prepare(kSr,55,1600);std::array<Stats,3>st{};std::array<float,1536>f{};
 for(const auto&p:profiles)for(double truth:freqs)for(double snr:snrs)for(auto seed:seeds){auto x=makePrefixStableExtendedVoiceLike(p,truth,snr,seed^(std::uint32_t)(truth*97));for(int i=0;i<1536;++i)f[(std::size_t)i]=(float)x[(std::size_t)i];for(std::size_t k=0;k<cps.size();++k){auto&s=st[k];++s.frames;auto a=d.analyse(f.data(),cps[k]);if(!a.valid||!a.ambiguousPrimitive)continue;++s.amb;double ce=std::abs(1200.0*std::log2(a.hz/truth));bool ok=ce<=100,hi=a.hz>1.5*truth;if(ok)++s.ambCorrect;else if(hi)++s.ambHigh;else ++s.ambOther;
 auto t0=std::chrono::steady_clock::now();auto e=measureAcf(f.data(),cps[k],a.lag);auto t1=std::chrono::steady_clock::now();s.us.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());if(!e.observable)continue;
 for(std::size_t r=0;r<rules.size();++r){bool v=e.promRatio>=rules[r].ratio&&e.peakDelta>=rules[r].delta;if(!v)continue;++s.votes[r];double lowered=.5*a.hz,le=std::abs(1200.0*std::log2(lowered/truth));if(hi&&le<=100)++s.fixed[r];else if(ok)++s.falseLower[r];else if(le>100)++s.wrongLower[r];}
 }}
 for(std::size_t k=0;k<cps.size();++k){auto s=st[k];std::sort(s.us.begin(),s.us.end());double mean=0;for(double v:s.us)mean+=v;if(!s.us.empty())mean/=s.us.size();auto pct=[&](double p){if(s.us.empty())return 0.0;return s.us[std::min(s.us.size()-1,(std::size_t)std::floor(p*(s.us.size()-1)))];};double act=s.frames?(double)s.amb/s.frames:0;
 std::cout<<std::fixed<<std::setprecision(4)<<"ACF_EMERGENCY phase="<<phase<<" set="<<name<<" samples="<<cps[k]<<" ms="<<(1000.0*cps[k]/kSr)<<" frames="<<s.frames<<" ambiguous="<<s.amb<<" amb_correct="<<s.ambCorrect<<" amb_high="<<s.ambHigh<<" amb_other="<<s.ambOther<<" activation_pct="<<(100*act)<<" mean_us="<<mean<<" p95_us="<<pct(.95)<<" max_us="<<pct(1)<<" weighted_mean_us="<<(mean*act);
 for(std::size_t r=0;r<rules.size();++r)std::cout<<" r"<<r<<"_ratio="<<rules[r].ratio<<" r"<<r<<"_delta="<<rules[r].delta<<" r"<<r<<"_votes="<<s.votes[r]<<" r"<<r<<"_corrected_high="<<s.fixed[r]<<" r"<<r<<"_false_lower_correct="<<s.falseLower[r]<<" r"<<r<<"_wrong_lower="<<s.wrongLower[r];std::cout<<'\n';}
}
}
int main(){
 constexpr std::array<std::uint32_t,8>A{0x0d95748fu,0x728eb658u,0x718bcd58u,0x82154aeeu,0x7b54a41du,0xc25a59b5u,0x9c30d539u,0x2af26013u};
 constexpr std::array<std::uint32_t,8>B{0xa4093822u,0x299f31d0u,0x082efa98u,0xec4e6c89u,0x452821e6u,0x38d01377u,0xbe5466cfu,0x34e90c6cu};
 constexpr std::array<std::uint32_t,8>C{0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u,0xc1059ed8u,0x367cd507u};
 constexpr std::array<std::uint32_t,8>D{0x6d2b79f5u,0x5a827999u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xcbbb9d5du};
 runSet("development","a",A);runSet("development","b",B);runSet("holdout","c",C);runSet("holdout","d",D);
}
