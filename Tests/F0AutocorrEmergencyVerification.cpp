#include "F0AutocorrEmergencyProbe.cpp"

namespace {
struct VerifyStats{int frames=0,amb=0,ambCorrect=0,ambHigh=0,ambOther=0,votes=0,fixed=0,falseLower=0,wrongLower=0,unresolvedHigh=0;std::vector<double>us;};
}

int main(){
 constexpr std::array<std::uint32_t,10>E{0x19a4c116u,0x327b23c6u,0x64f98fa7u,0xbe08c449u,0x1d8e4e27u,0x8ab6f9d2u,0x42c1eacdu,0xf4d50d87u,0x6f1d2b3au,0x93e7ac51u};
 constexpr std::array<double,12>freqs{110,123.4708,146.8324,164.8138,196,220,246.9417,293.6648,329.6276,440,659.2551,880};
 constexpr std::array<double,3>snrs{18,9,3};constexpr std::array<int,3>cps{1024,1280,1536};
 F0WholeNoteDetectorV1 d;d.prepare(kSr,55,1600);std::array<VerifyStats,3>st{};std::array<float,1536>f{};
 for(const auto&p:profiles)for(double truth:freqs)for(double snr:snrs)for(auto seed:E){auto x=makePrefixStableExtendedVoiceLike(p,truth,snr,seed^(std::uint32_t)(truth*97));for(int i=0;i<1536;++i)f[(std::size_t)i]=(float)x[(std::size_t)i];for(std::size_t k=0;k<cps.size();++k){auto&s=st[k];++s.frames;auto a=d.analyse(f.data(),cps[k]);if(!a.valid||!a.ambiguousPrimitive)continue;++s.amb;double ce=std::abs(1200.0*std::log2(a.hz/truth));bool ok=ce<=100,hi=a.hz>1.5*truth;if(ok)++s.ambCorrect;else if(hi)++s.ambHigh;else ++s.ambOther;
 auto t0=std::chrono::steady_clock::now();auto e=measureAcf(f.data(),cps[k],a.lag);auto t1=std::chrono::steady_clock::now();s.us.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());bool vote=e.observable&&e.promRatio>=1.20&&e.peakDelta>=0.02;if(!vote){if(hi)++s.unresolvedHigh;continue;}++s.votes;double lowered=.5*a.hz,le=std::abs(1200.0*std::log2(lowered/truth));if(hi&&le<=100)++s.fixed;else if(ok)++s.falseLower;else if(le>100)++s.wrongLower;}}
 for(std::size_t k=0;k<cps.size();++k){auto s=st[k];std::sort(s.us.begin(),s.us.end());double mean=0;for(double v:s.us)mean+=v;if(!s.us.empty())mean/=s.us.size();auto pct=[&](double p){if(s.us.empty())return 0.0;return s.us[std::min(s.us.size()-1,(std::size_t)std::floor(p*(s.us.size()-1)))];};double act=s.frames?(double)s.amb/s.frames:0;std::cout<<std::fixed<<std::setprecision(4)<<"ACF_FIXED_VERIFY samples="<<cps[k]<<" ms="<<(1000.0*cps[k]/kSr)<<" frames="<<s.frames<<" ambiguous="<<s.amb<<" amb_correct="<<s.ambCorrect<<" amb_high="<<s.ambHigh<<" amb_other="<<s.ambOther<<" votes="<<s.votes<<" corrected_high="<<s.fixed<<" false_lower_correct="<<s.falseLower<<" wrong_lower="<<s.wrongLower<<" unresolved_high="<<s.unresolvedHigh<<" activation_pct="<<(100*act)<<" mean_us="<<mean<<" p95_us="<<pct(.95)<<" max_us="<<pct(1)<<" weighted_mean_us="<<(mean*act)<<'\n';}
}
