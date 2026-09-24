#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExtendedEightProbe.cpp"

namespace
{
constexpr double kSr = 48000.0;
constexpr int kFft = 2048;
constexpr double kPi = 3.141592653589793238462643383279502884;
using C = std::complex<double>;

void fft(std::array<C, kFft>& a, bool inverse) noexcept
{
    for (int i = 1, j = 0; i < kFft; ++i)
    {
        int bit = kFft >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[(std::size_t)i], a[(std::size_t)j]);
    }
    for (int len = 2; len <= kFft; len <<= 1)
    {
        const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
        const C wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < kFft; i += len)
        {
            C w(1.0, 0.0);
            const int half = len >> 1;
            for (int j = 0; j < half; ++j)
            {
                const C u = a[(std::size_t)(i + j)];
                const C v = a[(std::size_t)(i + j + half)] * w;
                a[(std::size_t)(i + j)] = u + v;
                a[(std::size_t)(i + j + half)] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& z : a) z /= static_cast<double>(kFft);
}

struct CepEvidence
{
    bool observable = false;
    double shortPeak = 0.0;
    double longPeak = 0.0;
    double shortProm = 0.0;
    double longProm = 0.0;
    double ratio = 0.0;
    double normalizedMargin = 0.0;
    int shortQ = 0;
    int longQ = 0;
};

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    double m = *mid;
    if ((v.size() & 1u) == 0u)
    {
        const auto lo = std::max_element(v.begin(), mid);
        m = 0.5 * (m + *lo);
    }
    return m;
}

CepEvidence measureCepstrum(const float* samples, int n, int shortLag)
{
    if (!samples || n < 256 || n > 1536 || shortLag <= 0 || 2 * shortLag >= kFft / 2)
        return {};

    std::array<C, kFft> z {};
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += samples[i];
    mean /= static_cast<double>(n);

    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / static_cast<double>(n - 1));
        z[(std::size_t)i] = C((samples[i] - mean) * w, 0.0);
    }
    fft(z, false);
    for (int i = 0; i < kFft; ++i)
    {
        const double mag = std::abs(z[(std::size_t)i]);
        z[(std::size_t)i] = C(std::log(std::max(1.0e-12, mag)), 0.0);
    }
    fft(z, true);

    std::array<double, kFft / 2> cep {};
    for (int q = 0; q < kFft / 2; ++q)
        cep[(std::size_t)q] = z[(std::size_t)q].real();

    auto local = [&](int centre)
    {
        const int radius = std::max(2, static_cast<int>(std::ceil(0.05 * centre)));
        const int lo = std::max(2, centre - radius);
        const int hi = std::min(kFft / 2 - 2, centre + radius);
        double peak = -std::numeric_limits<double>::infinity();
        int peakQ = centre;
        for (int q = lo; q <= hi; ++q)
            if (cep[(std::size_t)q] > peak) { peak = cep[(std::size_t)q]; peakQ = q; }

        const int floorRadius = std::max(12, static_cast<int>(std::ceil(0.20 * centre)));
        const int fLo = std::max(2, centre - floorRadius);
        const int fHi = std::min(kFft / 2 - 2, centre + floorRadius);
        std::vector<double> floorSamples;
        floorSamples.reserve(static_cast<std::size_t>(fHi - fLo + 1));
        for (int q = fLo; q <= fHi; ++q)
            if (q < lo || q > hi) floorSamples.push_back(cep[(std::size_t)q]);
        const double floor = median(std::move(floorSamples));
        return std::array<double,3>{peak, peak - floor, static_cast<double>(peakQ)};
    };

    const auto s = local(shortLag);
    const auto l = local(2 * shortLag);

    std::vector<double> robust;
    const int qLo = std::max(2, static_cast<int>(std::floor(kSr / 1600.0)));
    const int qHi = std::min(kFft / 2 - 2, static_cast<int>(std::ceil(kSr / 55.0)));
    robust.reserve(static_cast<std::size_t>(qHi - qLo + 1));
    for (int q = qLo; q <= qHi; ++q) robust.push_back(std::abs(cep[(std::size_t)q]));
    const double scale = std::max(1.0e-12, median(std::move(robust)));

    CepEvidence e;
    e.observable = true;
    e.shortPeak = s[0];
    e.longPeak = l[0];
    e.shortProm = s[1];
    e.longProm = l[1];
    e.ratio = l[1] / std::max(1.0e-12, std::abs(s[1]));
    e.normalizedMargin = (l[1] - s[1]) / scale;
    e.shortQ = static_cast<int>(std::lround(s[2]));
    e.longQ = static_cast<int>(std::lround(l[2]));
    return e;
}

struct Rule { double ratio; double margin; };
constexpr std::array<Rule,8> rules {{
    {1.10, 0.5}, {1.20, 0.5}, {1.35, 0.5}, {1.50, 0.5},
    {1.10, 1.0}, {1.20, 1.0}, {1.35, 1.0}, {1.50, 1.0}
}};

struct Stats
{
    int frames = 0;
    int ambiguous = 0;
    int ambCorrect = 0;
    int ambHigh = 0;
    int ambLow = 0;
    int ambOther = 0;
    std::array<int,rules.size()> lowerVotes {};
    std::array<int,rules.size()> correctedHigh {};
    std::array<int,rules.size()> falseLowerCorrect {};
    std::array<int,rules.size()> wrongLower {};
    std::vector<double> emergencyUs;
};

template<std::size_t N>
void runSet(const char* phase, const char* name, const std::array<std::uint32_t,N>& seeds)
{
    constexpr std::array<double,12> freqs {110,123.4708,146.8324,164.8138,196,220,246.9417,293.6648,329.6276,440,659.2551,880};
    constexpr std::array<double,3> snrs {18,9,3};
    constexpr std::array<int,3> cps {1024,1280,1536};
    F0WholeNoteDetectorV1 detector; detector.prepare(kSr,55,1600);
    std::array<Stats,3> st {};
    std::array<float,1536> f {};

    for (const auto& profile : profiles)
        for (double truth : freqs)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto x = makePrefixStableExtendedVoiceLike(profile, truth, snr,
                        seed ^ static_cast<std::uint32_t>(truth * 97.0));
                    for (int i=0;i<1536;++i) f[(std::size_t)i]=static_cast<float>(x[(std::size_t)i]);

                    for (std::size_t k=0;k<cps.size();++k)
                    {
                        auto& s=st[k]; ++s.frames;
                        const auto a=detector.analyse(f.data(),cps[k]);
                        if (!a.valid || !a.ambiguousPrimitive) continue;
                        ++s.ambiguous;
                        const double cents=std::abs(1200.0*std::log2(a.hz/truth));
                        const bool correct=cents<=100.0;
                        const bool high=a.hz>1.5*truth;
                        const bool low=a.hz<0.75*truth;
                        if(correct)++s.ambCorrect; else if(high)++s.ambHigh; else if(low)++s.ambLow; else ++s.ambOther;

                        const auto t0=std::chrono::steady_clock::now();
                        const auto ce=measureCepstrum(f.data(),cps[k],a.lag);
                        const auto t1=std::chrono::steady_clock::now();
                        s.emergencyUs.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());
                        if(!ce.observable)continue;

                        for(std::size_t r=0;r<rules.size();++r)
                        {
                            const bool vote=ce.ratio>=rules[r].ratio && ce.normalizedMargin>=rules[r].margin;
                            if(!vote)continue;
                            ++s.lowerVotes[r];
                            const double lowered=0.5*a.hz;
                            const double le=std::abs(1200.0*std::log2(lowered/truth));
                            if(high && le<=100.0)++s.correctedHigh[r];
                            else if(correct)++s.falseLowerCorrect[r];
                            else if(le>100.0)++s.wrongLower[r];
                        }
                    }
                }

    for(std::size_t k=0;k<cps.size();++k)
    {
        auto s=st[k];
        std::sort(s.emergencyUs.begin(),s.emergencyUs.end());
        auto pct=[&](double p){if(s.emergencyUs.empty())return 0.0;const std::size_t i=std::min(s.emergencyUs.size()-1,(std::size_t)std::floor(p*(s.emergencyUs.size()-1)));return s.emergencyUs[i];};
        double mean=0;for(double v:s.emergencyUs)mean+=v;if(!s.emergencyUs.empty())mean/=s.emergencyUs.size();
        const double activation=s.frames?static_cast<double>(s.ambiguous)/s.frames:0.0;
        std::cout<<std::fixed<<std::setprecision(4)
            <<"CEPSTRUM_EMERGENCY phase="<<phase<<" set="<<name
            <<" samples="<<cps[k]<<" ms="<<(1000.0*cps[k]/kSr)
            <<" frames="<<s.frames<<" ambiguous="<<s.ambiguous
            <<" amb_correct="<<s.ambCorrect<<" amb_high="<<s.ambHigh
            <<" amb_low="<<s.ambLow<<" amb_other="<<s.ambOther
            <<" activation_pct="<<(100.0*activation)
            <<" mean_us="<<mean<<" p95_us="<<pct(.95)<<" max_us="<<pct(1.0)
            <<" weighted_mean_us="<<(mean*activation);
        for(std::size_t r=0;r<rules.size();++r)
            std::cout<<" r"<<r<<"_ratio="<<rules[r].ratio<<" r"<<r<<"_margin="<<rules[r].margin
                     <<" r"<<r<<"_votes="<<s.lowerVotes[r]
                     <<" r"<<r<<"_corrected_high="<<s.correctedHigh[r]
                     <<" r"<<r<<"_false_lower_correct="<<s.falseLowerCorrect[r]
                     <<" r"<<r<<"_wrong_lower="<<s.wrongLower[r];
        std::cout<<'\n';
    }
}
}

int main()
{
    constexpr std::array<std::uint32_t,8> A{0x0d95748fu,0x728eb658u,0x718bcd58u,0x82154aeeu,0x7b54a41du,0xc25a59b5u,0x9c30d539u,0x2af26013u};
    constexpr std::array<std::uint32_t,8> B{0xa4093822u,0x299f31d0u,0x082efa98u,0xec4e6c89u,0x452821e6u,0x38d01377u,0xbe5466cfu,0x34e90c6cu};
    constexpr std::array<std::uint32_t,8> C{0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u,0xc1059ed8u,0x367cd507u};
    constexpr std::array<std::uint32_t,8> D{0x6d2b79f5u,0x5a827999u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xcbbb9d5du};
    runSet("development","a",A);
    runSet("development","b",B);
    runSet("holdout","c",C);
    runSet("holdout","d",D);
}
