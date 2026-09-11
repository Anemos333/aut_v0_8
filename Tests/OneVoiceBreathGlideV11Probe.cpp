#include "../Source/SingleWetSpectralRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double sr = 48000.0;

double powerAt(const std::vector<float>& x, double hz, int start)
{
    double re=0.0, im=0.0;
    for (int n=start; n<(int)x.size(); ++n)
    {
        const double p=2.0*pi*hz*(double)n/sr;
        re += (double)x[(std::size_t)n]*std::cos(p);
        im -= (double)x[(std::size_t)n]*std::sin(p);
    }
    return re*re+im*im;
}

double estimate(const std::vector<float>& x, double expected, int start)
{
    double best=expected, bestP=-1.0;
    for(double f=expected-5.0; f<=expected+5.0001; f+=0.05)
    {
        const double p=powerAt(x,f,start);
        if(p>bestP){bestP=p;best=f;}
    }
    double l=best-0.08,r=best+0.08;
    for(int i=0;i<24;++i)
    {
        const double d=(r-l)/3.0,a=l+d,b=r-d;
        if(powerAt(x,a,start)<powerAt(x,b,start)) l=a; else r=b;
    }
    return 0.5*(l+r);
}

double cents(double measured,double expected)
{
    return 1200.0*std::log2(measured/expected);
}

std::vector<float> render(int frameSize,
                          double correctionCents,
                          const std::vector<double>& hz,
                          const std::vector<double>& amp,
                          double suppliedF0)
{
    SingleWetSpectralRenderer r;
    r.prepare(sr,frameSize);
    std::vector<float> out(96000);
    for(int n=0;n<(int)out.size();++n)
    {
        double s=0.0;
        for(std::size_t i=0;i<hz.size();++i)
            s += amp[i]*std::sin(2.0*pi*hz[i]*(double)n/sr + 0.173*(double)(i+1));
        out[(std::size_t)n]=r.processSample((float)s, correctionCents, 0.90f, suppliedF0);
    }
    return out;
}

double rmsDifference(const std::vector<float>& a,const std::vector<float>& b,int start)
{
    double e=0.0; int n=0;
    for(int i=start;i<(int)a.size() && i<(int)b.size();++i){const double d=(double)a[(std::size_t)i]-b[(std::size_t)i];e+=d*d;++n;}
    return n>0?std::sqrt(e/(double)n):0.0;
}
}

int main()
{
    bool ok=true;
    constexpr double shiftCt=137.60;
    const double ratio=std::exp2(shiftCt/1200.0);
    constexpr int start=24000;

    for(int fs: {512,256,128})
    {
        auto y=render(fs,shiftCt,{220.0},{0.22},173.7); // deliberately wrong F0 guide
        const double target=220.0*ratio;
        const double err=cents(estimate(y,target,start),target);
        const double ts=powerAt(y,target,start)/std::max(1e-20,powerAt(y,220.0,start));
        std::cerr<<"v11_frame_"<<fs<<"_tone_error_ct="<<err<<" target_source="<<ts<<'\n';
        ok &= std::abs(err)<0.35;
        ok &= ts>1000.0;
    }

    // Renderer output must not change when only the supplied F0 changes.
    auto liveA=render(256,shiftCt,{220.0,441.0,663.7},{0.18,0.07,0.035},110.0);
    auto liveB=render(256,shiftCt,{220.0,441.0,663.7},{0.18,0.07,0.035},440.0);
    const double f0Diff=rmsDifference(liveA,liveB,start);
    std::cerr<<"v11_live_renderer_f0_rms_diff="<<f0Diff<<'\n';
    ok &= f0Diff<1.0e-10;

    // Breath-like cloud is deliberately non-harmonic. It is not a separate
    // signal class; report how much of the same cloud follows the commanded ratio.
    std::vector<double> cloudHz {220.0};
    std::vector<double> cloudAmp {0.16};
    for(int i=0;i<24;++i)
    {
        cloudHz.push_back(1450.0 + 211.0*(double)i + 17.0*(double)((i*i+3*i)%7));
        cloudAmp.push_back(0.0045*(1.0-0.018*(double)i));
    }
    for(int fs: {512,256,128})
    {
        auto y=render(fs,shiftCt,cloudHz,cloudAmp,173.7);
        double target=0.0,source=0.0;
        for(std::size_t i=1;i<cloudHz.size();++i)
        {
            const double f=cloudHz[i];
            if(f*ratio<sr*0.48){target+=powerAt(y,f*ratio,start);source+=powerAt(y,f,start);}
        }
        const double ar=target/std::max(1e-20,source);
        const double vr=powerAt(y,220.0*ratio,start)/std::max(1e-20,powerAt(y,220.0,start));
        std::cerr<<"v11_frame_"<<fs<<"_air_target_source="<<ar<<" voice_target_source="<<vr<<'\n';
        if(fs>=256) ok &= ar>4.0; // Quality/Live must already move air with voice.
        if(fs==128) ok &= ar>0.50; // diagnostic floor; Experimental remains an explicit listen gate.
    }

    return ok?0:1;
}
