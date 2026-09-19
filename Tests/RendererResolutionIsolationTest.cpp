#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi=3.14159265358979323846;
constexpr double sr=48000.0;

double power(const std::vector<float>& s,double hz,int start,int count)
{
    double re=0.0,im=0.0;
    const int end=std::min(start+count,static_cast<int>(s.size()));
    for(int n=start;n<end;++n)
    {
        const double ph=2.0*pi*hz*static_cast<double>(n)/sr;
        const double x=s[static_cast<std::size_t>(n)];
        re+=x*std::cos(ph); im-=x*std::sin(ph);
    }
    return re*re+im*im;
}

double estimate(const std::vector<float>& s,double expected,int start,int count)
{
    double bestC=0.0,best=-1.0;
    for(double c=-60.0;c<=60.0001;c+=0.5)
    {
        const double hz=expected*std::exp2(c/1200.0);
        const double p=power(s,hz,start,count);
        if(p>best){best=p;bestC=c;}
    }
    double l=bestC-0.8,r=bestC+0.8;
    for(int i=0;i<15;++i)
    {
        const double d=(r-l)/3.0;
        const double ac=l+d,bc=r-d;
        if(power(s,expected*std::exp2(ac/1200.0),start,count)
           < power(s,expected*std::exp2(bc/1200.0),start,count)) l=ac;
        else r=bc;
    }
    return expected*std::exp2(0.5*(l+r)/1200.0);
}

struct Vowel { const char* name; std::array<double,3> f,bw; };

double env(double hz,const Vowel& v)
{
    double e=0.18;
    for(std::size_t i=0;i<3;++i)
    {
        const double d=(hz-v.f[i])/v.bw[i];
        e+=0.82*std::exp(-0.5*d*d);
    }
    return e;
}

std::vector<float> render(int frame,const Vowel& v,double cents)
{
    constexpr double f0=118.0;
    SingleWetSpectralRenderer r;
    r.prepare(sr,frame);
    std::vector<float> out(60000);
    for(int n=0;n<static_cast<int>(out.size());++n)
    {
        const double t=static_cast<double>(n)/sr;
        double x=0.0;
        for(int h=1;h<=24;++h)
        {
            const double hz=f0*h;
            if(hz>=8500.0) break;
            const double a=env(hz,v)/std::pow(static_cast<double>(h),1.06);
            x+=a*std::sin(2.0*pi*hz*t+0.19*h+0.009*h*h);
        }
        out[static_cast<std::size_t>(n)] =
            r.processSample(static_cast<float>(0.075*x),cents,0.0f);
    }
    return out;
}

void report(int frame,const Vowel& v,double correction)
{
    constexpr double source=118.0;
    const double target=source*std::exp2(correction/1200.0);
    const auto out=render(frame,v,correction);
    std::vector<double> errors;
    for(int start=16000;start+4096<=static_cast<int>(out.size());start+=6144)
    {
        const double measured=estimate(out,target,start,4096);
        errors.push_back(1200.0*std::log2(measured/target));
    }
    std::sort(errors.begin(),errors.end());
    double maxAbs=0.0;
    for(double e:errors) maxAbs=std::max(maxAbs,std::abs(e));
    const double med=errors[errors.size()/2];
    const double span=errors.back()-errors.front();
    std::cerr<<"RENDERER_RESOLUTION_ISOLATION"
             <<" frame="<<frame
             <<" vowel="<<v.name
             <<" correction_cents="<<correction
             <<" median_error_cents="<<med
             <<" max_abs_error_cents="<<maxAbs
             <<" error_span_cents="<<span
             <<'\n';
}
}

int main()
{
    const Vowel i{"i",{300.0,2290.0,3010.0},{75.0,150.0,210.0}};
    const Vowel u{"u",{330.0,870.0,2240.0},{80.0,110.0,190.0}};
    for(int frame:{256,512,1024})
        for(double correction:{0.0,37.0})
        {
            report(frame,i,correction);
            report(frame,u,correction);
        }
    std::cerr<<"RENDERER_RESOLUTION_ISOLATION=COMPLETE\n";
    return 0;
}
