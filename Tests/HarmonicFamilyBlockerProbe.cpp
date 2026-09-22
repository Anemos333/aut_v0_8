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
enum class Kind{normal,strongSecond,missingFundamental};
const char* kindName(Kind k) noexcept {
    switch(k){
        case Kind::normal:return "normal";
        case Kind::strongSecond:return "strong_second";
        case Kind::missingFundamental:return "missing_fundamental";
    }
    return "unknown";
}
float body(Kind k,double p) noexcept {
    if(k==Kind::strongSecond)
        return float(0.16*std::sin(p)+1.0*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
    if(k==Kind::missingFundamental)
        return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
    return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}
float clamp01(float x) noexcept {return std::clamp(x,0.0f,1.0f);}
float smoothStep(float lo,float hi,float x) noexcept {
    const float t=std::clamp((x-lo)/std::max(1e-9f,hi-lo),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
struct Frame {
    std::array<float,n> source{};
    std::array<float,n> residual{};
    std::array<double,n> hann{};
    double sourceEnergy=0,residualEnergy=0,windowEnergy=0;

    Frame(double f0,double snrDb,Kind kind,std::uint32_t seed){
        Rng rng{seed};
        const double amp=std::pow(10.0,-24.0/20.0);
        const double noise=amp/std::pow(10.0,snrDb/20.0);
        double phase=0.0,mean=0.0;
        for(int i=0;i<n;++i){
            phase+=twoPi*f0/sr;if(phase>=twoPi)phase-=twoPi;
            source[size_t(i)]=float(amp)*body(kind,phase)+float(noise)*rng.next();
            mean+=source[size_t(i)];
        }
        mean/=double(n);
        for(auto& x:source)x-=float(mean);

        double num=0,den=0;
        for(int i=1;i<n;++i){
            const double cur=source[size_t(i)],prev=source[size_t(i-1)];
            num+=cur*prev;den+=prev*prev;
        }
        const float predictor=float(std::clamp(num/std::max(1e-20,den),-0.92,0.92));
        residual[0]=source[0];
        for(int i=1;i<n;++i)
            residual[size_t(i)]=source[size_t(i)]-predictor*source[size_t(i-1)];

        for(int i=0;i<n;++i){
            const double w=0.5-0.5*std::cos(twoPi*double(i)/double(n-1));
            hann[size_t(i)]=w;windowEnergy+=w*w;
            const double a=double(source[size_t(i)])*w;
            const double b=double(residual[size_t(i)])*w;
            sourceEnergy+=a*a;residualEnergy+=b*b;
        }
    }

    float line(const std::array<float,n>& x,double energy,double hz) const noexcept {
        if(!(hz>0.0)||hz>=0.48*sr)return 0.0f;
        double re=0,im=0;
        for(int i=0;i<n;++i){
            const double y=double(x[size_t(i)])*hann[size_t(i)];
            const double ph=twoPi*hz*double(i)/sr;
            re+=y*std::cos(ph);im-=y*std::sin(ph);
        }
        const double norm=std::max(1e-20,energy*windowEnergy);
        return clamp01(float(std::sqrt(2.0*(re*re+im*im)/norm)));
    }

    float family(double f0,bool useResidual,bool subtractInter) const noexcept {
        const auto& x=useResidual?residual:source;
        const double energy=useResidual?residualEnergy:sourceEnergy;
        float harmonic=line(x,energy,f0),inter=0.0f;
        float hw=1.0f,iw=0.0f;
        for(int h=2;h<=6;++h){
            const double hf=f0*double(h);if(hf>=0.45*sr)break;
            const float w=1.0f/std::sqrt(float(h));
            harmonic+=w*line(x,energy,hf);hw+=w;
            const double ih=f0*(double(h)-0.5);
            if(ih<0.45*sr){inter+=w*line(x,energy,ih);iw+=w;}
        }
        const float hm=harmonic/std::max(1e-6f,hw);
        if(!subtractInter)return hm;
        const float im=iw>1e-6f?inter/iw:0.0f;
        return smoothStep(0.025f,0.30f,hm-0.78f*im);
    }
};

struct Variant{const char* name;bool residual;bool inter;};
constexpr std::array<Variant,4> variants{{
    {"residual_current",true,true},
    {"source_current",false,true},
    {"residual_no_inter",true,false},
    {"source_no_inter",false,false}
}};

void run(double f0,double snr,Kind kind,std::uint32_t seed){
    Frame f(f0,snr,kind,seed);
    for(const auto& v:variants){
        const auto t0=std::chrono::steady_clock::now();
        const float fs=f.family(f0,v.residual,v.inter);
        const float ds=f.family(2.0*f0,v.residual,v.inter);
        const float hs=f0>=110.0?f.family(0.5*f0,v.residual,v.inter):-1.0f;
        const float ts=f.family(3.0*f0,v.residual,v.inter);
        const auto t1=std::chrono::steady_clock::now();

        double winnerHz=f0;float winner=fs;
        auto consider=[&](double hz,float s){if(s>winner){winner=s;winnerHz=hz;}};
        consider(2*f0,ds); if(hs>=0)consider(0.5*f0,hs); consider(3*f0,ts);

        std::cout<<std::fixed<<std::setprecision(6)
                 <<"FAMILY_BLOCKER_PROBE"
                 <<" variant="<<v.name
                 <<" kind="<<kindName(kind)
                 <<" hz="<<f0<<" snr="<<snr<<" seed="<<seed
                 <<" true="<<fs<<" double="<<ds<<" half="<<hs<<" triple="<<ts
                 <<" margin_double="<<(fs-ds)
                 <<" winner_hz="<<winnerHz
                 <<" true_wins="<<(std::abs(winnerHz-f0)<1e-9?1:0)
                 <<" us="<<std::chrono::duration<double,std::micro>(t1-t0).count()
                 <<"\n";
    }
}
}

int main(){
    constexpr std::array<double,6> freqs{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,2> snrs{6.0,3.0};
    constexpr std::array<std::uint32_t,3> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u};
    constexpr std::array<Kind,3> kinds{Kind::normal,Kind::strongSecond,Kind::missingFundamental};
    for(auto seed:seeds)for(auto kind:kinds)for(double hz:freqs)for(double snr:snrs)run(hz,snr,kind,seed);
}
