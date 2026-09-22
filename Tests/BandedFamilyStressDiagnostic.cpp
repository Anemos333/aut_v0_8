#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr=48000.0;
constexpr double pi=3.14159265358979323846;
constexpr int analysisLength=480;

struct Rng{std::uint32_t s;float next()noexcept{s^=s<<13;s^=s>>17;s^=s<<5;return float(s&0xffffu)/32767.5f-1.0f;}};

enum class Kind{normal,strongSecond,missingFundamental,onsetBurst,white};
const char* name(Kind k) noexcept {
    switch(k){
        case Kind::normal:return "normal";
        case Kind::strongSecond:return "strong_second";
        case Kind::missingFundamental:return "missing_fundamental";
        case Kind::onsetBurst:return "onset_burst";
        case Kind::white:return "white";
    }
    return "unknown";
}

float body(Kind k,double p) noexcept {
    if(k==Kind::strongSecond)
        return float(0.16*std::sin(p)+1.00*std::sin(2*p+0.17)+0.38*std::sin(3*p-0.31)+0.18*std::sin(4*p+0.49)+0.09*std::sin(5*p-0.63));
    if(k==Kind::missingFundamental)
        return float(0.90*std::sin(2*p+0.17)+0.58*std::sin(3*p-0.31)+0.34*std::sin(4*p+0.49)+0.20*std::sin(5*p-0.63)+0.12*std::sin(6*p+0.27));
    return float(0.72*std::sin(p)+0.34*std::sin(2*p+0.17)+0.21*std::sin(3*p-0.31)+0.13*std::sin(4*p+0.49)+0.08*std::sin(5*p-0.63));
}

double cents(double measured,double target) noexcept {
    if(!(measured>0.0)||!(target>0.0)) return std::numeric_limits<double>::infinity();
    return 1200.0*std::log2(measured/target);
}

struct Candidate{
    double frequency=0.0;
    float contrast=0.0f;
    int band=-1;
    int divisor=1;
    double parent=0.0;
};

void run(double targetHz,double snrDb,Kind kind,std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(sr);
    tracker.setRange(55.0f,1600.0f);

    Rng rng{seed};
    double phase=0.0;
    const double amp=std::pow(10.0,-24.0/20.0);
    const double noiseAmp=amp/std::pow(10.0,snrDb/20.0);
    ModernPitchEngine::PitchObservation o;

    for(int i=0;i<analysisLength;++i){
        float x=0.0f;
        if(kind==Kind::white){
            x=float(amp)*rng.next();
        }else{
            phase+=2*pi*targetHz/sr;
            if(phase>=2*pi)phase-=2*pi;
            x=float(amp)*body(kind,phase)+float(noiseAmp)*rng.next();
            if(kind==Kind::onsetBurst && i<int(0.003*sr))
                x+=float(3.5*amp)*rng.next();
        }
        tracker.processSample(x,o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws{};
    (void)tracker.measureCoordinate(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        sr,55.0f,1600.0f,analysisLength,ws);

    const auto sourceCorrelation=[&](int lag) noexcept {
        if(lag<=0||lag>=analysisLength-8)return -1.0f;
        double corr=0,ea=0,eb=0;
        const int overlap=analysisLength-lag;
        for(int i=0;i<overlap;++i){
            const double a=ws.frame[size_t(i)];
            const double b=ws.frame[size_t(i+lag)];
            corr+=a*b;ea+=a*a;eb+=b*b;
        }
        const double den=std::sqrt(std::max(1e-20,ea*eb));
        return den>0?float(corr/den):-1.0f;
    };

    const auto refineTau=[&](int tau) noexcept {
        int best=tau;float peak=sourceCorrelation(tau);
        for(int off=-2;off<=2;++off){
            const int t=tau+off;
            if(t<2||t>=analysisLength-8)continue;
            const float p=sourceCorrelation(t);
            if(p>peak){peak=p;best=t;}
        }
        double refined=double(best);
        if(best>2&&best<analysisLength-9){
            const double l=sourceCorrelation(best-1),c=sourceCorrelation(best),r=sourceCorrelation(best+1);
            const double den=l-2*c+r;
            if(std::abs(den)>1e-12)
                refined+=std::clamp(0.5*(l-r)/den,-0.75,0.75);
        }
        return sr/refined;
    };

    std::array<double,analysisLength> hann{};
    double signalEnergy=0,windowEnergy=0;
    for(int i=0;i<analysisLength;++i){
        const double w=0.5-0.5*std::cos(2*pi*double(i)/double(analysisLength-1));
        hann[size_t(i)]=w;
        const double y=double(ws.voiceResidualFrame[size_t(i)])*w;
        signalEnergy+=y*y;windowEnergy+=w*w;
    }

    const auto line=[&](double hz) noexcept {
        if(!(hz>0)||hz>=0.45*sr)return 0.0f;
        double re=0,im=0;
        for(int i=0;i<analysisLength;++i){
            const double y=double(ws.voiceResidualFrame[size_t(i)])*hann[size_t(i)];
            const double ph=2*pi*hz*double(i)/sr;
            re+=y*std::cos(ph);im-=y*std::sin(ph);
        }
        return std::clamp(float(std::sqrt(2*(re*re+im*im)/std::max(1e-20,signalEnergy*windowEnergy))),0.0f,1.0f);
    };

    const auto contrast=[&](double f0) noexcept {
        if(!(f0>=55.0&&f0<=1600.0))return 0.0f;
        float hs=line(f0),is=0,hw=1,iw=0;
        for(int h=2;h<=6;++h){
            const double hf=f0*double(h); if(hf>=0.45*sr)break;
            const float w=1.0f/std::sqrt(float(h));
            hs+=w*line(hf);hw+=w;
            const double inter=f0*(double(h)-0.5);
            if(inter<0.45*sr){is+=w*line(inter);iw+=w;}
        }
        const float hm=hs/std::max(1e-6f,hw);
        const float im=iw>1e-6f?is/iw:0;
        const float raw=hm-0.78f*im;
        const float t=std::clamp((raw-0.025f)/(0.30f-0.025f),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    };

    constexpr std::array<std::array<double,2>,4> bands{{
        {55.0,230.0},{230.0,460.0},{460.0,900.0},{900.0,1600.0}
    }};

    std::array<Candidate,24> candidates{};
    int count=0;
    for(int band=0;band<4;++band){
        const double low=bands[size_t(band)][0],high=bands[size_t(band)][1];
        const int tauMin=std::max(2,int(std::floor(sr/high)));
        const int tauMax=std::min(analysisLength-16,int(std::ceil(sr/low)));
        if(tauMin>tauMax)continue;
        int bestTau=tauMin;float best=ws.difference[size_t(tauMin)];
        for(int tau=tauMin+1;tau<=tauMax;++tau){
            const float v=ws.difference[size_t(tau)];
            if(v<best){best=v;bestTau=tau;}
        }
        const double parent=refineTau(bestTau);
        for(int divisor=1;divisor<=4;++divisor){
            const double hz=parent/double(divisor);
            if(hz<55||hz>1600)continue;
            bool dup=false;
            for(int i=0;i<count;++i)
                if(std::abs(cents(candidates[size_t(i)].frequency,hz))<18.0){dup=true;break;}
            if(dup||count>=int(candidates.size()))continue;
            auto& c=candidates[size_t(count++)];
            c.frequency=hz;c.parent=parent;c.divisor=divisor;c.band=band;c.contrast=contrast(hz);
        }
    }

    std::sort(candidates.begin(),candidates.begin()+count,[](const Candidate&a,const Candidate&b){return a.contrast>b.contrast;});
    const Candidate best=count?candidates[0]:Candidate{};
    const double err=kind==Kind::white?0.0:cents(best.frequency,targetHz);
    const bool familyCorrect=kind!=Kind::white&&std::abs(err)<=85.0;
    const bool fineCorrect=kind!=Kind::white&&std::abs(err)<=1.5;

    std::cout<<std::fixed<<std::setprecision(6)
             <<"BANDED_FAMILY_STRESS kind="<<name(kind)
             <<" hz="<<targetHz<<" snr="<<snrDb<<" seed="<<seed
             <<" selected="<<best.frequency
             <<" abs_cents="<<(kind==Kind::white?-1.0:std::abs(err))
             <<" contrast="<<best.contrast
             <<" band="<<best.band<<" divisor="<<best.divisor<<" parent="<<best.parent
             <<" family_correct="<<(familyCorrect?1:0)
             <<" fine_correct="<<(fineCorrect?1:0)
             <<" candidate_count="<<count
             <<"\n";
}
}

int main(){
    constexpr std::array<double,6> hz{82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,4> snr{12.0,6.0,3.0,0.0};
    constexpr std::array<std::uint32_t,4> seeds{0x1234567u,0x51f15e5du,0x9e3779b9u,0x3141592u};
    for(auto seed:seeds){
        for(auto kind:{Kind::normal,Kind::strongSecond,Kind::missingFundamental,Kind::onsetBurst})
            for(double f:hz)for(double s:snr)run(f,s,kind,seed);
        run(220.0,0.0,Kind::white,seed);
    }
}
