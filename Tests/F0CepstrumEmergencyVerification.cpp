#include "F0CepstrumEmergencyProbe.cpp"

int main()
{
    constexpr std::array<std::uint32_t,10> E{
        0x1f4d3b29u,0x7a6c5e41u,0x92b7d31fu,0x4e8a63c5u,0xd6f102abu,
        0x35c79e14u,0xa83d5f72u,0x6b21c9e7u,0xf04a7d36u,0x58e3b19cu
    };
    constexpr std::array<double,12> freqs{110,123.4708,146.8324,164.8138,196,220,246.9417,293.6648,329.6276,440,659.2551,880};
    constexpr std::array<double,3> snrs{18,9,3};
    constexpr std::array<int,3> cps{1024,1280,1536};
    constexpr double ratioThreshold=1.20;
    constexpr double marginThreshold=1.0;

    F0WholeNoteDetectorV1 detector; detector.prepare(kSr,55,1600);
    std::array<float,1536> f{};

    for(int cp:cps)
    {
        int frames=0, ambiguous=0, ambCorrect=0, ambHigh=0, ambOther=0;
        int votes=0, correctedHigh=0, falseLowerCorrect=0, wrongLower=0, unresolvedHigh=0;
        std::vector<double> costs;

        for(const auto& profile:profiles)
            for(double truth:freqs)
                for(double snr:snrs)
                    for(auto seed:E)
                    {
                        ++frames;
                        const auto x=makePrefixStableExtendedVoiceLike(profile,truth,snr,
                            seed^static_cast<std::uint32_t>(truth*97.0));
                        for(int i=0;i<1536;++i)f[(std::size_t)i]=static_cast<float>(x[(std::size_t)i]);
                        const auto a=detector.analyse(f.data(),cp);
                        if(!a.valid||!a.ambiguousPrimitive)continue;
                        ++ambiguous;
                        const double baseErr=std::abs(1200.0*std::log2(a.hz/truth));
                        const bool correct=baseErr<=100.0;
                        const bool high=a.hz>1.5*truth;
                        if(correct)++ambCorrect;else if(high)++ambHigh;else++ambOther;

                        const auto t0=std::chrono::steady_clock::now();
                        const auto ce=measureCepstrum(f.data(),cp,a.lag);
                        const auto t1=std::chrono::steady_clock::now();
                        costs.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());
                        const bool vote=ce.observable&&ce.ratio>=ratioThreshold&&ce.normalizedMargin>=marginThreshold;
                        if(!vote){if(high)++unresolvedHigh;continue;}
                        ++votes;
                        const double lower=0.5*a.hz;
                        const double lowerErr=std::abs(1200.0*std::log2(lower/truth));
                        if(high&&lowerErr<=100.0)++correctedHigh;
                        else if(correct)++falseLowerCorrect;
                        else if(lowerErr>100.0)++wrongLower;
                    }

        std::sort(costs.begin(),costs.end());
        double mean=0;for(double x:costs)mean+=x;if(!costs.empty())mean/=costs.size();
        auto pct=[&](double p){if(costs.empty())return 0.0;return costs[std::min(costs.size()-1,(std::size_t)std::floor(p*(costs.size()-1)))];};
        const double activation=frames?static_cast<double>(ambiguous)/frames:0.0;
        std::cout<<std::fixed<<std::setprecision(4)
                 <<"CEPSTRUM_FIXED_VERIFY samples="<<cp
                 <<" ms="<<(1000.0*cp/kSr)
                 <<" frames="<<frames<<" ambiguous="<<ambiguous
                 <<" amb_correct="<<ambCorrect<<" amb_high="<<ambHigh<<" amb_other="<<ambOther
                 <<" votes="<<votes<<" corrected_high="<<correctedHigh
                 <<" false_lower_correct="<<falseLowerCorrect<<" wrong_lower="<<wrongLower
                 <<" unresolved_high="<<unresolvedHigh
                 <<" activation_pct="<<(100.0*activation)
                 <<" mean_us="<<mean<<" p95_us="<<pct(.95)<<" max_us="<<pct(1.0)
                 <<" weighted_mean_us="<<(mean*activation)<<'\n';
    }
}
