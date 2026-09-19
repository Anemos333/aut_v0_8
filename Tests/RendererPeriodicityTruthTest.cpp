#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double pi=3.14159265358979323846;
constexpr double sr=48000.0;

struct Vowel
{
    const char* name;
    std::array<double,3> f;
    std::array<double,3> bw;
};

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

std::vector<float> makeSource(double f0,const Vowel& vowel,int count)
{
    std::vector<float> source(static_cast<std::size_t>(count));
    for(int n=0;n<count;++n)
    {
        const double t=static_cast<double>(n)/sr;
        double x=0.0;
        for(int h=1;h<=28;++h)
        {
            const double hz=f0*h;
            if(hz>=8500.0) break;
            const double a=env(hz,vowel)/std::pow(static_cast<double>(h),1.06);
            x+=a*std::sin(2.0*pi*hz*t+0.19*h+0.009*h*h);
        }
        source[static_cast<std::size_t>(n)]=static_cast<float>(0.075*x);
    }
    return source;
}

std::vector<float> render(const std::vector<float>& source,int frame,double cents)
{
    SingleWetSpectralRenderer r;
    r.prepare(sr,frame);
    std::vector<float> out(source.size());
    for(std::size_t n=0;n<source.size();++n)
        out[n]=r.processSample(source[n],cents,0.0f);
    return out;
}

double normalizedCorrelation(const std::vector<float>& x,
                             int start,
                             int count,
                             int lag)
{
    const int first=std::max(start+lag,lag);
    const int end=std::min(start+count,static_cast<int>(x.size()));
    double xy=0.0,xx=0.0,yy=0.0;
    for(int n=first;n<end;++n)
    {
        const double a=x[static_cast<std::size_t>(n)];
        const double b=x[static_cast<std::size_t>(n-lag)];
        xy+=a*b; xx+=a*a; yy+=b*b;
    }
    return xy/std::sqrt(std::max(1.0e-30,xx*yy));
}

struct PeriodEstimate
{
    double hz=0.0;
    double peak=0.0;
    double halfPeriodScore=0.0;
    double doublePeriodScore=0.0;
};

PeriodEstimate estimatePeriod(const std::vector<float>& x,
                              double expectedHz,
                              int start,
                              int count)
{
    const double expectedPeriod=sr/expectedHz;
    const int minLag=std::max(2,static_cast<int>(std::floor(expectedPeriod*0.88)));
    const int maxLag=static_cast<int>(std::ceil(expectedPeriod*1.12));

    int bestLag=minLag;
    double best=-2.0;
    for(int lag=minLag;lag<=maxLag;++lag)
    {
        const double c=normalizedCorrelation(x,start,count,lag);
        if(c>best){best=c;bestLag=lag;}
    }

    double refined=static_cast<double>(bestLag);
    if(bestLag>minLag && bestLag<maxLag)
    {
        const double ym=normalizedCorrelation(x,start,count,bestLag-1);
        const double y0=best;
        const double yp=normalizedCorrelation(x,start,count,bestLag+1);
        const double denom=ym-2.0*y0+yp;
        if(std::abs(denom)>1.0e-12)
            refined += std::clamp(0.5*(ym-yp)/denom,-1.0,1.0);
    }

    const int halfLag=std::max(1,static_cast<int>(std::lround(0.5*expectedPeriod)));
    const int doubleLag=std::max(1,static_cast<int>(std::lround(2.0*expectedPeriod)));

    PeriodEstimate result;
    result.hz=sr/refined;
    result.peak=best;
    result.halfPeriodScore=normalizedCorrelation(x,start,count,halfLag);
    result.doublePeriodScore=normalizedCorrelation(x,start,count,doubleLag);
    return result;
}

void report(const char* kind,
            int frame,
            const Vowel& vowel,
            double sourceF0,
            double correction,
            const std::vector<float>& x,
            double expectedF0,
            int start)
{
    constexpr int count=16384;
    const auto e=estimatePeriod(x,expectedF0,start,count);
    const double error=1200.0*std::log2(e.hz/expectedF0);
    std::cerr<<"RENDERER_PERIODICITY_TRUTH"
             <<" kind="<<kind
             <<" frame="<<frame
             <<" vowel="<<vowel.name
             <<" source_f0="<<sourceF0
             <<" correction_cents="<<correction
             <<" expected_f0="<<expectedF0
             <<" measured_f0="<<e.hz
             <<" error_cents="<<error
             <<" period_corr="<<e.peak
             <<" half_period_corr="<<e.halfPeriodScore
             <<" double_period_corr="<<e.doublePeriodScore
             <<'\n';
}
}

int main()
{
    const Vowel i{"i",{300.0,2290.0,3010.0},{75.0,150.0,210.0}};
    const Vowel u{"u",{330.0,870.0,2240.0},{80.0,110.0,190.0}};

    constexpr int total=72000;
    constexpr double sourceF0=118.0;

    for(const auto& vowel:{i,u})
    {
        const auto source=makeSource(sourceF0,vowel,total);
        report("dry_reference",0,vowel,sourceF0,0.0,source,sourceF0,18000);

        for(int frame:{256,512,1024})
        {
            for(double correction:{0.0,37.0,-63.0,700.0})
            {
                const auto out=render(source,frame,correction);
                const double target=sourceF0*std::exp2(correction/1200.0);
                report("renderer",frame,vowel,sourceF0,correction,out,target,18000);
            }
        }
    }

    std::cerr<<"RENDERER_PERIODICITY_TRUTH=COMPLETE\n";
    return 0;
}
