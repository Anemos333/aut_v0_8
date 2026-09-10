#include "../Source/SingleWetSpectralRenderer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double sr = 48000.0;

double tonePower(const std::vector<float>& signal, double hz, int start)
{
    double re=0.0, im=0.0;
    for (int n=start; n<(int)signal.size(); ++n)
    {
        const double ph = 2.0*pi*hz*(double)n/sr;
        re += (double)signal[(std::size_t)n]*std::cos(ph);
        im -= (double)signal[(std::size_t)n]*std::sin(ph);
    }
    return re*re+im*im;
}

double estimate(const std::vector<float>& signal, double expected, int start)
{
    double best=expected, bestP=-1.0;
    for(double f=expected-4.0; f<=expected+4.0001; f+=0.05)
    {
        const double p=tonePower(signal,f,start);
        if(p>bestP){bestP=p;best=f;}
    }
    double l=best-0.08,r=best+0.08;
    for(int i=0;i<24;++i)
    {
        const double d=(r-l)/3.0,a=l+d,b=r-d;
        if(tonePower(signal,a,start)<tonePower(signal,b,start)) l=a; else r=b;
    }
    return 0.5*(l+r);
}

double cents(double measured,double expected)
{
    return 1200.0*std::log2(measured/expected);
}

std::vector<float> renderComponents(int frameSize,
                                    double correctionCents,
                                    const std::vector<double>& hz,
                                    const std::vector<double>& amp,
                                    float formant=0.9f)
{
    SingleWetSpectralRenderer r;
    r.prepare(sr,frameSize);
    std::vector<float> out(96000);
    for(int n=0;n<(int)out.size();++n)
    {
        double x=0.0;
        for(std::size_t i=0;i<hz.size();++i)
            x += amp[i]*std::sin(2.0*pi*hz[i]*(double)n/sr + 0.271*(double)(i+1));
        out[(std::size_t)n]=r.processSample((float)x,correctionCents,formant,173.70);
    }
    return out;
}

bool check(bool ok,const char* name)
{
    std::cerr<<name<<'='<<(ok?"PASS":"FAIL")<<'\n';
    return ok;
}
}

int main()
{
    bool ok=true;
    constexpr double shiftCt=137.60;
    const double ratio=std::exp2(shiftCt/1200.0);

    for(int fs: {512,256,128})
    {
        auto out=renderComponents(fs,shiftCt,{220.0},{0.22});
        const double expected=220.0*ratio;
        const double measured=estimate(out,expected,24000);
        const double err=cents(measured,expected);
        const double tr=tonePower(out,expected,24000)/std::max(1e-20,tonePower(out,220.0,24000));
        std::cerr<<"v10_frame_"<<fs<<"_tone_error_ct="<<err<<" target_source="<<tr<<'\n';
        ok &= check(std::abs(err)<0.35, fs==512?"v10_quality_ratio":fs==256?"v10_live_ratio":"v10_experimental_ratio");
        ok &= check(tr>1000.0, fs==512?"v10_quality_no_source_copy":fs==256?"v10_live_no_source_copy":"v10_experimental_no_source_copy");
    }

    const std::vector<double> harmonicHz {173.70,347.40,521.10,694.80,868.50,1042.20,1215.90,1389.60};
    const std::vector<double> harmonicAmp {0.12,0.060,0.040,0.030,0.024,0.020,0.017,0.015};
    const std::vector<double> inharmonicHz {277.0,401.0,593.0,877.0,1237.0,1691.0};
    const std::vector<double> inharmonicAmp {0.065,0.055,0.045,0.038,0.030,0.024};

    // A deterministic breath-like cloud: deliberately non-harmonic, dense, and
    // low-level. Every component must be transported by the same ratio as the voice.
    std::vector<double> cloudHz;
    std::vector<double> cloudAmp;
    for(int i=0;i<24;++i)
    {
        const double f=1450.0 + 211.0*(double)i + 17.0*(double)((i*i+3*i)%7);
        cloudHz.push_back(f);
        cloudAmp.push_back(0.0045*(1.0-0.018*(double)i));
    }
    // Add a vocal fundamental so the probe specifically checks voice+air unity.
    cloudHz.insert(cloudHz.begin(),220.0);
    cloudAmp.insert(cloudAmp.begin(),0.16);

    for(int fs: {512,256,128})
    {
        auto h=renderComponents(fs,shiftCt,harmonicHz,harmonicAmp,0.0f);
        double ht=0.0,hs=0.0,maxErr=0.0;
        for(double f:harmonicHz)
        {
            const double e=f*ratio;
            ht += tonePower(h,e,24000);
            hs += tonePower(h,f,24000);
            if(e<3000.0) maxErr=std::max(maxErr,std::abs(cents(estimate(h,e,24000),e)));
        }
        const double hr=ht/std::max(1e-20,hs);
        std::cerr<<"v10_frame_"<<fs<<"_harmonic_family_ratio="<<hr<<" max_error_ct="<<maxErr<<'\n';
        ok &= check(hr>20.0,fs==512?"v10_quality_harmonic_transport":fs==256?"v10_live_harmonic_transport":"v10_experimental_harmonic_transport");

        auto in=renderComponents(fs,shiftCt,inharmonicHz,inharmonicAmp,0.0f);
        double it=0.0,is=0.0;
        for(double f:inharmonicHz){it+=tonePower(in,f*ratio,24000);is+=tonePower(in,f,24000);}
        const double ir=it/std::max(1e-20,is);
        std::cerr<<"v10_frame_"<<fs<<"_inharmonic_target_source="<<ir<<'\n';
        ok &= check(ir>10.0,fs==512?"v10_quality_inharmonic_one_path":fs==256?"v10_live_inharmonic_one_path":"v10_experimental_inharmonic_one_path");

        auto cloud=renderComponents(fs,shiftCt,cloudHz,cloudAmp,0.0f);
        double ct=0.0,cs=0.0;
        for(std::size_t i=1;i<cloudHz.size();++i)
        {
            const double f=cloudHz[i];
            if(f*ratio<sr*0.48){ct+=tonePower(cloud,f*ratio,24000);cs+=tonePower(cloud,f,24000);}
        }
        const double cr=ct/std::max(1e-20,cs);
        const double voiceTarget=tonePower(cloud,220.0*ratio,24000);
        const double voiceSource=tonePower(cloud,220.0,24000);
        std::cerr<<"v10_frame_"<<fs<<"_air_target_source="<<cr<<" voice_target_source="<<voiceTarget/std::max(1e-20,voiceSource)<<'\n';
        ok &= check(cr>4.0,fs==512?"v10_quality_air_moves_with_voice":fs==256?"v10_live_air_moves_with_voice":"v10_experimental_air_moves_with_voice");
    }

    return ok?0:1;
}
