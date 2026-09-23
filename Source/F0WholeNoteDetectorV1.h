#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

class F0WholeNoteDetectorV1 final
{
public:
    struct Analysis
    {
        bool valid = false;
        double hz = 0.0;
        double periodicity = 0.0;
        int lag = 0;
        int samples = 0;
        bool primitiveLowered = false;
        bool exclusiveLowerWitness = false;
    };

    void prepare(double sampleRate, double minimumHz = 55.0, double maximumHz = 1600.0) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
        minimumHz_ = std::max(20.0, minimumHz);
        maximumHz_ = std::max(minimumHz_ + 1.0, maximumHz);
    }

    [[nodiscard]] Analysis analyse(const float* samples, int sampleCount) const noexcept
    {
        if (samples == nullptr || sampleCount < 96 || sampleCount > maxAnalysisSamples)
            return {};

        std::array<double, maxAnalysisSamples> x {};
        double mean = 0.0;
        double energy = 0.0;
        for (int n = 0; n < sampleCount; ++n)
        {
            double s = static_cast<double>(samples[n]);
            if (!std::isfinite(s) || std::fpclassify(s) == FP_SUBNORMAL) s = 0.0;
            x[(std::size_t)n] = s;
            mean += s;
            energy += s * s;
        }
        mean /= (double)sampleCount;
        const double rms = std::sqrt(energy / (double)sampleCount);
        if (!(rms > 1.0e-7)) return {};
        for (int n=0;n<sampleCount;++n) x[(std::size_t)n] -= mean;

        const int lagMin = std::max(2, (int)std::floor(sampleRate_ / maximumHz_));
        const int lagMax = std::min(sampleCount - 8, (int)std::ceil(sampleRate_ / minimumHz_));
        if (lagMin >= lagMax || lagMax >= maxAnalysisSamples) return {};

        std::array<double, maxAnalysisSamples> corr {};
        double strongest = -1.0;
        for (int lag=lagMin; lag<=lagMax; ++lag)
        {
            const int overlap = sampleCount-lag;
            double ab=0,aa=0,bb=0;
            for(int n=0;n<overlap;++n){double a=x[(std::size_t)n], b=x[(std::size_t)(n+lag)];ab+=a*b;aa+=a*a;bb+=b*b;}
            const double den=std::sqrt(std::max(1.0e-30,aa*bb));
            const double v=den>0?ab/den:-1.0;
            corr[(std::size_t)lag]=v; strongest=std::max(strongest,v);
        }
        const double acceptance=std::max(0.55,strongest-0.08);
        constexpr double decorrelationThreshold=0.35;
        bool decorrelated=false; int bestLag=0; double bestCorr=-1;
        for(int lag=lagMin+1;lag<lagMax;++lag){double v=corr[(std::size_t)lag];if(v<=decorrelationThreshold){decorrelated=true;continue;} if(!decorrelated||v<acceptance)continue; if(v<corr[(std::size_t)(lag-1)]||v<corr[(std::size_t)(lag+1)])continue; bestLag=lag;bestCorr=v;break;}
        if(bestLag==0){for(int lag=lagMin;lag<=lagMax;++lag){double v=corr[(std::size_t)lag];if(v>bestCorr){bestCorr=v;bestLag=lag;}}}
        if(bestLag<=0||bestCorr<0.48)return {};

        double refinedLag=(double)bestLag;
        if(bestLag>lagMin&&bestLag<lagMax){double l=corr[(std::size_t)(bestLag-1)],c=corr[(std::size_t)bestLag],r=corr[(std::size_t)(bestLag+1)];double den=l-2*c+r;if(std::abs(den)>1e-12){double d=0.5*(l-r)/den;if(std::abs(d)<=1.0)refinedLag+=d;}}
        double hz=sampleRate_/refinedLag; int finalLag=bestLag; bool primitiveLowered=false,exclusiveLower=false; double primitiveRatio=1.0;
        if(2*bestLag<sampleCount){int overlap=sampleCount-2*bestLag;if(overlap>=40){double one=mismatch(x,sampleCount,overlap,bestLag),two=mismatch(x,sampleCount,overlap,2*bestLag);primitiveRatio=two/std::max(1e-12,one);if(primitiveRatio<=0.65&&0.5*hz>=minimumHz_){hz*=0.5;finalLag*=2;primitiveLowered=true;}}}
        if(!primitiveLowered&&0.5*hz>=minimumHz_){auto alt=alternation(x,sampleCount,bestLag);if(alt.observable&&alt.score>=3.0){hz*=0.5;finalLag*=2;primitiveLowered=true;}}
        if(!primitiveLowered&&sampleCount>=samplesForMs(18.667)&&0.5*hz>=minimumHz_){auto alt=alternation(x,sampleCount,bestLag);if(alt.observable&&primitiveRatio<=0.85&&alt.significance>=1.0){hz*=0.5;finalLag*=2;primitiveLowered=true;}}
        if(!primitiveLowered&&sampleCount>=samplesForMs(21.333)&&0.5*hz>=minimumHz_){auto low=exclusiveLowerFamily(x,sampleCount,bestLag);const bool standardExclusive=low.observable&&low.witnesses8>=3;const bool strongTemporalExclusive=low.observable&&low.witnesses8>=2&&low.localRatio<=0.85;if(standardExclusive||strongTemporalExclusive){hz=low.hz;finalLag=low.lag;primitiveLowered=true;exclusiveLower=true;}}
        if(!(hz>=minimumHz_&&hz<=maximumHz_)||!std::isfinite(hz))return {};
        if(2*finalLag>=sampleCount)return {};
        return {true,hz,bestCorr,finalLag,sampleCount,primitiveLowered,exclusiveLower};
    }
private:
    struct Alternation{bool observable=false;double score=0,significance=0;};
    struct LowerFamily{bool observable=false;double hz=0;int lag=0,witnesses8=0;double localRatio=1.0;};
    static constexpr int maxAnalysisSamples=1536;
    static constexpr double pi=3.141592653589793238462643383279502884;
    int samplesForMs(double ms) const noexcept{return (int)std::lround(0.001*ms*sampleRate_);}
    static double mismatch(const std::array<double,maxAnalysisSamples>&x,int sampleCount,int overlap,int lag)noexcept{if(lag<=0||lag>=sampleCount||overlap<=0||overlap+lag>sampleCount)return std::numeric_limits<double>::infinity();double diff=0,en=0;for(int n=0;n<overlap;++n){double a=x[(std::size_t)n],b=x[(std::size_t)(n+lag)],d=a-b;diff+=d*d;en+=a*a+b*b;}return diff/std::max(1e-30,en);}
    static Alternation alternation(const std::array<double,maxAnalysisSamples>&x,int sampleCount,int lag)noexcept{if(lag<=0)return{};int cycles=sampleCount/lag;if(cycles<4)return{};int ec=(cycles+1)/2,oc=cycles/2;if(ec<2||oc<2)return{};double between=0,within=0,en=0;for(int p=0;p<lag;++p){double em=0,om=0;int ne=0,no=0;for(int cy=0;cy<cycles;++cy){int idx=cy*lag+p;if(idx>=sampleCount)break;double s=x[(std::size_t)idx];en+=s*s;if((cy&1)==0){em+=s;++ne;}else{om+=s;++no;}}if(ne==0||no==0)continue;em/=ne;om/=no;double d=em-om;between+=d*d;for(int cy=0;cy<cycles;++cy){int idx=cy*lag+p;if(idx>=sampleCount)break;double s=x[(std::size_t)idx],m=(cy&1)==0?em:om,e=s-m;within+=e*e;}}double nb=between/std::max(1e-30,en/(double)cycles),nw=within/std::max(1e-30,en),score=nb/std::max(1e-12,nw),groups=(double)(ec*oc)/(double)(ec+oc);return{true,score,score*groups};}
    static double projectionPower(const std::array<double,maxAnalysisSamples>&x,int sampleCount,double sr,double hz)noexcept{if(!(hz>0)||hz>=0.48*sr)return 0;double re=0,im=0;for(int n=0;n<sampleCount;++n){double w=0.5-0.5*std::cos(2*pi*n/(double)(sampleCount-1)),p=2*pi*hz*n/sr,s=x[(std::size_t)n]*w;re+=s*std::cos(p);im-=s*std::sin(p);}return re*re+im*im;}
    LowerFamily exclusiveLowerFamily(const std::array<double,maxAnalysisSamples>&x,int sampleCount,int shortLag)const noexcept{if(shortLag<=0)return{};int centre=2*shortLag,radius=std::max(2,(int)std::ceil(0.05*centre)),lo=std::max(shortLag+2,centre-radius),hi=std::min(sampleCount-40,centre+radius);if(lo>hi)return{};int overlap=sampleCount-hi;if(overlap<40)return{};const double shortMismatch=mismatch(x,sampleCount,overlap,shortLag);double best=std::numeric_limits<double>::infinity();int bestLag=0;for(int lag=lo;lag<=hi;++lag){double m=mismatch(x,sampleCount,overlap,lag);if(m<best){best=m;bestLag=lag;}}if(bestLag<=0)return{};const double localRatio=best/std::max(1e-12,shortMismatch);if(localRatio>1.20)return{};double lowHz=sampleRate_/bestLag;if(!(lowHz>=minimumHz_))return{};int wits=0,measured=0;for(int k:{1,3,5,7}){double hz=lowHz*k;if(hz>=0.44*sampleRate_)continue;double cp=projectionPower(x,sampleCount,sampleRate_,hz);std::array<double,4>side{projectionPower(x,sampleCount,sampleRate_,hz-0.40*lowHz),projectionPower(x,sampleCount,sampleRate_,hz-0.30*lowHz),projectionPower(x,sampleCount,sampleRate_,hz+0.30*lowHz),projectionPower(x,sampleCount,sampleRate_,hz+0.40*lowHz)};std::sort(side.begin(),side.end());double floor=0.5*(side[1]+side[2]),ratio=cp/std::max(1e-20,floor);if(ratio>=8.0)++wits;++measured;}if(measured<3)return{};return{true,lowHz,bestLag,wits,localRatio};}
    double sampleRate_=48000.0,minimumHz_=55.0,maximumHz_=1600.0;
};
