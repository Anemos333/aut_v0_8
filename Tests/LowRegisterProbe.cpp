#include "../Source/ModernPitchEngine.h"
#include <array>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>
#include <algorithm>
constexpr double pi=3.14159265358979323846;
std::array<double,12> scale(){std::array<double,12>s{};for(int i=0;i<12;++i)s[i]=std::exp2(i/12.0);return s;}
ModernPitchEngine::Metering run(const std::vector<float>&in,double sr=48000,int bs=64){auto x=in;ModernPitchEngine e; e.prepare(sr,bs,1,ModernPitchEngine::LatencyMode::live);ModernPitchEngine::Parameters p; p.minimumPitchHz=35;p.maximumPitchHz=1000;p.amount=1;auto sc=scale();for(int o=0;o<(int)x.size();o+=bs){int n=std::min(bs,(int)x.size()-o);float* ptr=x.data()+o;juce::AudioBuffer<float>b(&ptr,1,n);e.process(b,sc.data(),12,261.625565,p);}return e.getMetering();}
std::vector<float> harmonic(double f, std::vector<double> amps, double noise=0, double sub=0,double sr=48000,double sec=3){int n=std::lround(sr*sec);std::vector<float>x(n);std::mt19937 g(123);std::normal_distribution<float>d(0,noise);for(int i=0;i<n;++i){double t=i/sr,v=sub*std::sin(2*pi*(f/2)*t);for(size_t h=0;h<amps.size();++h)v+=amps[h]*std::sin(2*pi*f*(h+1)*t+0.17*h);x[i]=float(v+d(g));}return x;}
int main(){for(double f:{45.,55.,65.406,73.416,82.407,98.,110.,130.813,164.814,196.,220.}){for(auto a:{std::vector<double>{.03,.55,.25,.13,.08},std::vector<double>{0,.52,.32,.18,.09},std::vector<double>{.12,.35,.28,.20,.12,.07}}){auto m=run(harmonic(f,a,.015));std::cout<<f<<","<<m.detectedPitchHz<<","<<m.confidence<<","<<m.consensus<<","<<m.detectorSupport<<","<<m.octaveState<<"\n";}auto m=run(harmonic(f,{.08,.45,.25},.01,.18));std::cout<<f<<","<<m.detectedPitchHz<<","<<m.confidence<<","<<m.consensus<<","<<m.detectorSupport<<","<<m.octaveState<<",sub\n";}}
