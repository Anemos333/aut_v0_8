#include "../Source/ModernPitchEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr double pi = 3.1415926535897932384626433832795;

std::array<double, 12> chromatic() {
    std::array<double, 12> s{};
    for (int i=0;i<12;++i) s[static_cast<std::size_t>(i)] = std::exp2(i/12.0);
    return s;
}

struct Result { std::vector<std::vector<float>> audio; ModernPitchEngine::Metering meter; };

Result render(const std::vector<std::vector<float>>& input, double sr, int block,
              ModernPitchEngine::LatencyMode mode, ModernPitchEngine::Parameters p)
{
    const int ch = static_cast<int>(input.size());
    const int n = ch ? static_cast<int>(input[0].size()) : 0;
    Result r{input,{}};
    ModernPitchEngine e;
    e.prepare(sr, block, ch, mode);
    auto s=chromatic();
    for (int off=0;off<n;off+=block) {
        int count=std::min(block,n-off);
        std::vector<float*> ptrs(static_cast<std::size_t>(ch));
        for(int c=0;c<ch;++c) ptrs[static_cast<std::size_t>(c)] = r.audio[static_cast<std::size_t>(c)].data()+off;
        juce::AudioBuffer<float> view(ptrs.data(), ch, count);
        e.process(view,s.data(),static_cast<int>(s.size()),261.625565,p);
    }
    r.meter=e.getMetering();
    return r;
}

std::vector<float> sine(double f,double sec,double sr,float amp=0.25f,double phase0=0.0){
 int n=static_cast<int>(std::llround(sec*sr)); std::vector<float>x(static_cast<std::size_t>(n));
 double ph=phase0,inc=2*pi*f/sr; for(float&v:x){v=amp*std::sin(ph);ph+=inc;if(ph>=2*pi)ph-=2*pi;} return x;
}

double rms(const std::vector<float>&x,int start=0){double s=0;int n=0;for(int i=start;i<(int)x.size();++i){if(std::isfinite(x[i])){s+=double(x[i])*x[i];++n;}}return n?std::sqrt(s/n):0;}
double corr(const std::vector<float>&a,const std::vector<float>&b,int start=0){double aa=0,bb=0,ab=0;int n=std::min(a.size(),b.size());for(int i=start;i<n;++i){ab+=double(a[i])*b[i];aa+=double(a[i])*a[i];bb+=double(b[i])*b[i];}return ab/std::sqrt(std::max(1e-30,aa*bb));}

void probePitch(double f, ModernPitchEngine::LatencyMode mode){
 auto p=ModernPitchEngine::Parameters{}; p.amount=1;p.retuneTimeMs=8;p.transitionTimeMs=35;p.minimumPitchHz=35;p.maximumPitchHz=1600;
 auto x=sine(f,3.0,48000); auto r=render({x},48000,64,mode,p);
 std::cout<<"pitch f="<<f<<" mode="<<(int)mode<<" det="<<r.meter.detectedPitchHz<<" conf="<<r.meter.confidence<<" voice="<<r.meter.voicing<<" support="<<r.meter.detectorSupport<<" corr="<<r.meter.correctionCents<<" wet="<<r.meter.wetMix<<" harm="<<r.meter.harmonicity<<" noise="<<r.meter.noisePath<<" rmsIn="<<rms(x,4096)<<" rmsOut="<<rms(r.audio[0],4096)<<"\n";
}

void probeAmountZero(){
 auto p=ModernPitchEngine::Parameters{};p.amount=0;
 auto l=sine(220,2,48000,.3f,0); auto rr=sine(220,2,48000,.3f,.7);
 for(int m=0;m<3;++m){auto r=render({l,rr},48000,127,(ModernPitchEngine::LatencyMode)m,p);int lat=m==0?128:m==1?256:512;
 std::vector<float> dl(l.size(),0),dr(rr.size(),0);for(int i=lat;i<(int)l.size();++i){dl[i]=l[i-lat];dr[i]=rr[i-lat];}
 std::cout<<"amount0 mode="<<m<<" maxL=";float md=0;for(size_t i=0;i<l.size();++i)md=std::max(md,std::abs(r.audio[0][i]-dl[i]));std::cout<<md<<" maxR=";md=0;for(size_t i=0;i<l.size();++i)md=std::max(md,std::abs(r.audio[1][i]-dr[i]));std::cout<<md<<" widthCorrIn="<<corr(l,rr,0)<<" widthCorrOut="<<corr(r.audio[0],r.audio[1],lat+1024)<<"\n";}
}

} // namespace

int main(){
 probeAmountZero();
 for(double f: {55.0,65.406,73.416,82.407,98.0,110.0,130.813,164.814,196.0,220.0,230.0}) for(int m=0;m<3;++m) probePitch(f,(ModernPitchEngine::LatencyMode)m);
 return 0;
}
