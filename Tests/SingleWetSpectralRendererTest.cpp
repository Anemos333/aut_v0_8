#include "SingleWetSpectralRenderer.h"
#include <cmath>
#include <iostream>
#include <tuple>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
bool check(bool c,const char* n){std::cerr<<n<<'='<<(c?"PASS":"FAIL")<<'\
'; return c;}
double tonePower(const std::vector<float>& x,double sr,double hz,int start){double re=0,im=0; for(int i=start;i<(int)x.size();++i){double p=2*pi*hz*i/sr; re+=x[i]*std::cos(p); im-=x[i]*std::sin(p);} return re*re+im*im;}
std::vector<float> render(float conf,float body,double cents){SingleWetSpectralRenderer r; r.prepare(48000.0,512); SingleWetSpectralRenderer::Context c; c.detectedPitchHz=220; c.confidence=conf; c.voicing=conf; c.consensus=conf; c.noteBodyLatched=true; c.noteBodyConfidence=body; c.stableMusicalBody=true; std::vector<float> out(48000); for(int i=0;i<(int)out.size();++i){float in=0.22f*std::sin(2*pi*220.0*i/48000.0); out[i]=r.processSample(in,cents,0.9f,c);} return out;}
}
int main(){bool ok=true; const double target=220.0*std::exp2(100.0/1200.0); for(auto [conf,body,name]: std::vector<std::tuple<float,float,const char*>>{{0.95f,0.95f,"strong_body"},{0.05f,0.92f,"latched_low_confidence"}}){auto x=render(conf,body,100.0); double pt=tonePower(x,48000,target,12000), ps=tonePower(x,48000,220.0,12000); std::cerr<<name<<"_target_source_ratio="<<pt/std::max(1e-20,ps)<<'\
'; ok&=check(pt>4.0*ps,name);} auto unity=render(0.95f,0.95f,0.0); double p220=tonePower(unity,48000,220.0,12000), p233=tonePower(unity,48000,target,12000); ok&=check(p220>4.0*p233,"zero_correction_preserves_source_pitch"); return ok?0:1;}
