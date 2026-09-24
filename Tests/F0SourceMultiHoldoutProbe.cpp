#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExtendedEightProbe.cpp"
namespace{
struct S{int cases=0,correct=0,high=0,low=0,other=0,amb=0,ambCorrect=0,ambHigh=0,ambLow=0,ambOther=0;};
template<std::size_t N>void run(const char*name,const std::array<std::uint32_t,N>&seeds){constexpr std::array<double,12>freqs{110,123.4708,146.8324,164.8138,196,220,246.9417,293.6648,329.6276,440,659.2551,880};constexpr std::array<double,3>snrs{18,9,3};constexpr std::array<int,3>cps{1024,1280,1536};std::array<S,3>st{};F0WholeNoteDetectorV1 d;d.prepare(48000,55,1600);std::array<float,1536>f{};for(const auto&p:profiles)for(double truth:freqs)for(double snr:snrs)for(auto seed:seeds){auto x=makePrefixStableExtendedVoiceLike(p,truth,snr,seed^static_cast<std::uint32_t>(truth*97));for(int i=0;i<1536;++i)f[(std::size_t)i]=(float)x[(std::size_t)i];for(std::size_t k=0;k<cps.size();++k){auto&a=st[k];++a.cases;auto r=d.analyse(f.data(),cps[k]);if(!r.valid)continue;double e=std::abs(1200.0*std::log2(r.hz/truth));bool ok=e<=100,hi=r.hz>1.5*truth,lo=r.hz<.75*truth;if(r.ambiguousPrimitive){++a.amb;if(ok)++a.ambCorrect;else if(hi)++a.ambHigh;else if(lo)++a.ambLow;else++a.ambOther;continue;}if(ok)++a.correct;else if(hi)++a.high;else if(lo)++a.low;else++a.other;}}for(std::size_t k=0;k<cps.size();++k){auto&a=st[k];std::cout<<"SOURCE_MULTI_HOLDOUT set="<<name<<" samples="<<cps[k]<<" ms="<<std::fixed<<std::setprecision(4)<<(1000.0*cps[k]/48000.0)<<" cases="<<a.cases<<" correct="<<a.correct<<" high="<<a.high<<" low="<<a.low<<" other="<<a.other<<" ambiguous="<<a.amb<<" ambiguous_correct="<<a.ambCorrect<<" ambiguous_high="<<a.ambHigh<<" ambiguous_low="<<a.ambLow<<" ambiguous_other="<<a.ambOther<<'\n';}}
}
int main(){constexpr std::array<std::uint32_t,8>A{0x0d95748fu,0x728eb658u,0x718bcd58u,0x82154aeeu,0x7b54a41du,0xc25a59b5u,0x9c30d539u,0x2af26013u},B{0xa4093822u,0x299f31d0u,0x082efa98u,0xec4e6c89u,0x452821e6u,0x38d01377u,0xbe5466cfu,0x34e90c6cu},C{0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u,0xc1059ed8u,0x367cd507u};run("a",A);run("b",B);run("c",C);}
