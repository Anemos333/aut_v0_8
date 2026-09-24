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
        bool valid=false; double hz=0.0,periodicity=0.0; int lag=0,samples=0;
        bool primitiveLowered=false,exclusiveLowerWitness=false,ambiguousPrimitive=false;
    };
    void prepare(double sr,double minHz=55.0,double maxHz=1600.0) noexcept
    { sampleRate_=std::isfinite(sr)?std::max(8000.0,sr):48000.0; minimumHz_=std::max(20.0,minHz); maximumHz_=std::max(minimumHz_+1.0,maxHz); }

    [[nodiscard]] Analysis analyse(const float* samples,int sampleCount) const noexcept
    {
        if(!samples||sampleCount<96||sampleCount>maxAnalysisSamples)return{};
        std::array<double,maxAnalysisSamples>x{}; double mean=0,en=0;
        for(int n=0;n<sampleCount;++n){double s=(double)samples[n];if(!std::isfinite(s)||std::fpclassify(s)==FP_SUBNORMAL)s=0;x[(std::size_t)n]=s;mean+=s;en+=s*s;}
        mean/=sampleCount;if(!(std::sqrt(en/sampleCount)>1e-7))return{};for(int n=0;n<sampleCount;++n)x[(std::size_t)n]-=mean;
        const int lagMin=std::max(2,(int)std::floor(sampleRate_/maximumHz_)),lagMax=std::min(sampleCount-8,(int)std::ceil(sampleRate_/minimumHz_));if(lagMin>=lagMax||lagMax>=maxAnalysisSamples)return{};
        std::array<double,maxAnalysisSamples>corr{};double strongest=-1;
        for(int lag=lagMin;lag<=lagMax;++lag){double ab=0,aa=0,bb=0;int overlap=sampleCount-lag;for(int n=0;n<overlap;++n){double a=x[(std::size_t)n],b=x[(std::size_t)(n+lag)];ab+=a*b;aa+=a*a;bb+=b*b;}double den=std::sqrt(std::max(1e-30,aa*bb)),v=den>0?ab/den:-1;corr[(std::size_t)lag]=v;strongest=std::max(strongest,v);}
        const double acceptance=std::max(0.55,strongest-0.08);bool decorrelated=false;int bestLag=0;double bestCorr=-1;
        for(int lag=lagMin+1;lag<lagMax;++lag){double v=corr[(std::size_t)lag];if(v<=0.35){decorrelated=true;continue;}if(!decorrelated||v<acceptance)continue;if(v<corr[(std::size_t)(lag-1)]||v<corr[(std::size_t)(lag+1)])continue;bestLag=lag;bestCorr=v;break;}
        if(!bestLag)for(int lag=lagMin;lag<=lagMax;++lag)if(corr[(std::size_t)lag]>bestCorr){bestCorr=corr[(std::size_t)lag];bestLag=lag;}if(bestLag<=0||bestCorr<0.48)return{};
        double refinedLag=bestLag;if(bestLag>lagMin&&bestLag<lagMax){double l=corr[(std::size_t)(bestLag-1)],c=corr[(std::size_t)bestLag],r=corr[(std::size_t)(bestLag+1)],d=l-2*c+r;if(std::abs(d)>1e-12){double q=.5*(l-r)/d;if(std::abs(q)<=1)refinedLag+=q;}}
        double hz=sampleRate_/refinedLag;int finalLag=bestLag;bool lowered=false,exclusive=false,ambiguous=false;double primitiveRatio=1.0;
        if(2*bestLag<sampleCount){int overlap=sampleCount-2*bestLag;if(overlap>=40){double one=mismatch(x,sampleCount,overlap,bestLag),two=mismatch(x,sampleCount,overlap,2*bestLag);primitiveRatio=two/std::max(1e-12,one);if(primitiveRatio<=.65&&.5*hz>=minimumHz_){hz*=.5;finalLag*=2;lowered=true;}}}
        if(!lowered&&.5*hz>=minimumHz_){auto a=alternation(x,sampleCount,bestLag);if(a.observable&&a.score>=3.0){hz*=.5;finalLag*=2;lowered=true;}}
        // The old weak long-consensus no longer lowers F0: it caused a 220 -> 110 artificial low.
        if(!lowered&&sampleCount>=samplesForMs(21.333)&&.5*hz>=minimumHz_){auto low=exclusiveLowerFamily(x,sampleCount,bestLag);if(low.observable&&low.witnesses8>=3){hz=low.hz;finalLag=low.lag;lowered=true;exclusive=true;}else if(low.observable&&low.witnesses8==2&&low.localRatio<=.85)ambiguous=true;}
        if(!(hz>=minimumHz_&&hz<=maximumHz_)||!std::isfinite(hz)||2*finalLag>=sampleCount)return{};
        return{true,hz,bestCorr,finalLag,sampleCount,lowered,exclusive,ambiguous};
    }
private:
    struct Alternation{bool observable=false;double score=0,significance=0;};
    struct LowerFamily{bool observable=false;double hz=0;int lag=0,witnesses8=0;double localRatio=1;};
    static constexpr int maxAnalysisSamples=1536;static constexpr double pi=3.141592653589793238462643383279502884;
    int samplesForMs(double ms)const noexcept{return(int)std::lround(.001*ms*sampleRate_);}
    static double mismatch(const std::array<double,maxAnalysisSamples>&x,int sampleCount,int overlap,int lag)noexcept{if(lag<=0||lag>=sampleCount||overlap<=0||overlap+lag>sampleCount)return std::numeric_limits<double>::infinity();double d=0,e=0;for(int n=0;n<overlap;++n){double a=x[(std::size_t)n],b=x[(std::size_t)(n+lag)],q=a-b;d+=q*q;e+=a*a+b*b;}return d/std::max(1e-30,e);}
    static Alternation alternation(const std::array<double,maxAnalysisSamples>&x,int sampleCount,int lag)noexcept{if(lag<=0)return{};int cycles=sampleCount/lag;if(cycles<4)return{};int ec=(cycles+1)/2,oc=cycles/2;if(ec<2||oc<2)return{};double between=0,within=0,en=0;for(int p=0;p<lag;++p){double em=0,om=0;int ne=0,no=0;for(int cy=0;cy<cycles;++cy){int i=cy*lag+p;if(i>=sampleCount)break;double s=x[(std::size_t)i];en+=s*s;if((cy&1)==0){em+=s;++ne;}else{om+=s;++no;}}if(!ne||!no)continue;em/=ne;om/=no;double q=em-om;between+=q*q;for(int cy=0;cy<cycles;++cy){int i=cy*lag+p;if(i>=sampleCount)break;double s=x[(std::size_t)i],m=(cy&1)==0?em:om,r=s-m;within+=r*r;}}double nb=between/std::max(1e-30,en/cycles),nw=within/std::max(1e-30,en),score=nb/std::max(1e-12,nw),groups=(double)(ec*oc)/(ec+oc);return{true,score,score*groups};}
    static double projectionPower(const std::array<double,maxAnalysisSamples>&x,int n,double sr,double hz)noexcept{if(!(hz>0)||hz>=.48*sr)return 0;double re=0,im=0;for(int i=0;i<n;++i){double w=.5-.5*std::cos(2*pi*i/(double)(n-1)),p=2*pi*hz*i/sr,s=x[(std::size_t)i]*w;re+=s*std::cos(p);im-=s*std::sin(p);}return re*re+im*im;}
    LowerFamily exclusiveLowerFamily(const std::array<double,maxAnalysisSamples>&x,int n,int shortLag)const noexcept
    {if(shortLag<=0)return{};int centre=2*shortLag,radius=std::max(2,(int)std::ceil(.05*centre)),lo=std::max(shortLag+2,centre-radius),hi=std::min(n-40,centre+radius);if(lo>hi)return{};int overlap=n-hi;if(overlap<40)return{};double shortM=mismatch(x,n,overlap,shortLag),best=std::numeric_limits<double>::infinity();int bestLag=0;for(int lag=lo;lag<=hi;++lag){double m=mismatch(x,n,overlap,lag);if(m<best){best=m;bestLag=lag;}}if(bestLag<=0)return{};double localRatio=best/std::max(1e-12,shortM);if(localRatio>1.20)return{};double lowHz=sampleRate_/bestLag;if(!(lowHz>=minimumHz_))return{};int wits=0,measured=0;for(int k:{1,3,5,7}){double hz=lowHz*k;if(hz>=.44*sampleRate_)continue;double cp=projectionPower(x,n,sampleRate_,hz);std::array<double,4>s{projectionPower(x,n,sampleRate_,hz-.40*lowHz),projectionPower(x,n,sampleRate_,hz-.30*lowHz),projectionPower(x,n,sampleRate_,hz+.30*lowHz),projectionPower(x,n,sampleRate_,hz+.40*lowHz)};std::sort(s.begin(),s.end());double floor=.5*(s[1]+s[2]);if(cp/std::max(1e-20,floor)>=8.0)++wits;++measured;}if(measured<3)return{};return{true,lowHz,bestLag,wits,localRatio};}
    double sampleRate_=48000.0,minimumHz_=55.0,maximumHz_=1600.0;
};
