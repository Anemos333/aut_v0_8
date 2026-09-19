#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

double tonePowerRange(const std::vector<float>& signal,
                      double frequencyHz,
                      int startSample,
                      int sampleCount)
{
    double re = 0.0, im = 0.0;
    const int end = std::min(startSample + sampleCount,
                             static_cast<int>(signal.size()));
    for (int n = startSample; n < end; ++n)
    {
        const double phase = 2.0 * pi * frequencyHz
            * static_cast<double>(n) / sampleRate;
        const double x = signal[static_cast<std::size_t>(n)];
        re += x * std::cos(phase);
        im -= x * std::sin(phase);
    }
    return re * re + im * im;
}

double estimateNear(const std::vector<float>& signal,
                    double expectedHz,
                    int startSample,
                    int sampleCount)
{
    double bestCents = 0.0;
    double best = -1.0;
    for (double cents = -50.0; cents <= 50.0; cents += 0.5)
    {
        const double hz = expectedHz * std::exp2(cents / 1200.0);
        const double p = tonePowerRange(signal, hz, startSample, sampleCount);
        if (p > best)
        {
            best = p;
            bestCents = cents;
        }
    }
    double left = bestCents - 0.8, right = bestCents + 0.8;
    for (int i = 0; i < 15; ++i)
    {
        const double third = (right-left)/3.0;
        const double ac = left + third, bc = right - third;
        const double ahz = expectedHz * std::exp2(ac/1200.0);
        const double bhz = expectedHz * std::exp2(bc/1200.0);
        if (tonePowerRange(signal, ahz, startSample, sampleCount)
            < tonePowerRange(signal, bhz, startSample, sampleCount))
            left = ac;
        else
            right = bc;
    }
    return expectedHz * std::exp2(0.5*(left+right)/1200.0);
}

double cents(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

struct Vowel
{
    const char* name;
    std::array<double,3> f;
    std::array<double,3> bw;
};

double envelope(double hz, const Vowel& v)
{
    double e=0.18;
    for (std::size_t i=0;i<v.f.size();++i)
    {
        const double d=(hz-v.f[i])/v.bw[i];
        e += 0.82*std::exp(-0.5*d*d);
    }
    return e;
}

std::vector<float> render(int frameSize,
                          double sourceF0,
                          double correctionCents,
                          const Vowel& vowel,
                          float formant)
{
    SingleWetSpectralRenderer r;
    r.prepare(sampleRate, frameSize);
    constexpr int count=60000;
    std::vector<float> out(count);
    for(int n=0;n<count;++n)
    {
        const double t=static_cast<double>(n)/sampleRate;
        double x=0.0;
        for(int h=1;h<=24;++h)
        {
            const double hz=sourceF0*h;
            if(hz>=8500.0) break;
            const double a=envelope(hz,vowel)/std::pow(static_cast<double>(h),1.06);
            const double ph=0.19*h+0.009*h*h;
            x += a*std::sin(2.0*pi*hz*t+ph);
        }
        out[static_cast<std::size_t>(n)] =
            r.processSample(static_cast<float>(0.075*x), correctionCents, formant);
    }
    return out;
}

void report(int frame,
            const Vowel& vowel,
            double sourceF0,
            double correction,
            float formant)
{
    const double target=sourceF0*std::exp2(correction/1200.0);
    const auto out=render(frame,sourceF0,correction,vowel,formant);
    std::vector<double> errors;
    for(int start=12000;start+4096<=static_cast<int>(out.size());start+=6144)
    {
        const double measured=estimateNear(out,target,start,4096);
        errors.push_back(cents(measured,target));
    }
    std::sort(errors.begin(),errors.end());
    double maxAbs=0.0;
    for(double e:errors) maxAbs=std::max(maxAbs,std::abs(e));
    const double span=errors.empty()?0.0:errors.back()-errors.front();
    const double median=errors.empty()?0.0:errors[errors.size()/2];

    std::cerr<<"RENDERER_FORMANT_ISOLATION"
             <<" frame="<<frame
             <<" vowel="<<vowel.name
             <<" source_f0="<<sourceF0
             <<" correction_cents="<<correction
             <<" formant="<<formant
             <<" median_error_cents="<<median
             <<" max_abs_error_cents="<<maxAbs
             <<" error_span_cents="<<span
             <<'\n';
}
}

int main()
{
    const Vowel a{"a",{730.0,1090.0,2440.0},{95.0,125.0,180.0}};
    const Vowel i{"i",{300.0,2290.0,3010.0},{75.0,150.0,210.0}};
    const Vowel u{"u",{330.0,870.0,2240.0},{80.0,110.0,190.0}};

    for(int frame:{512,256})
    {
        for(float formant:{0.0f,0.92f})
        {
            report(frame,i,118.0,37.0,formant);
            report(frame,u,118.0,37.0,formant);
            report(frame,a,196.0,700.0,formant);
            report(frame,i,196.0,700.0,formant);
            report(frame,u,196.0,700.0,formant);
        }
    }
    std::cerr<<"RENDERER_FORMANT_ISOLATION=COMPLETE\n";
    return 0;
}
