#include "Effect.h"
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <array>
namespace bt {
namespace {
class Builtin final:public Effect {
 QString type; double rate; std::vector<Parameter> p;std::vector<float> delay;size_t cursor=0;float env=0,gain=1,low[2]{},high[2]{},damped[8]{};std::array<std::vector<float>,12> room;size_t frame=0;
public:
 Builtin(QString t,double sr):type(std::move(t)),rate(sr),delay(type=="Delay"?size_t(sr*2)*2:2,0){
 if(type=="Reverb"){const double lengths[]={.0297,.0371,.0411,.0437};for(int c=0;c<2;++c)for(int k=0;k<4;++k)room[c*4+k].resize(size_t(sr*(lengths[k]*1.6+.002)));for(int k=8;k<12;++k)room[k].resize(size_t(sr*(k%2?.0017:.005)));}
 if(type=="EQ")p={{"Low",.5},{"Mid",.5},{"High",.5}};
 else if(type=="Compressor")p={{"Threshold",.65},{"Ratio",.3},{"Makeup",.25}};
 else if(type=="Gate")p={{"Threshold",.2},{"Release",.3}};
 else if(type=="Delay")p={{"Time",.25},{"Feedback",.35},{"Mix",.2}};
 else if(type=="Reverb")p={{"Size",.45},{"Decay",.55},{"Mix",.2}};
 else if(type=="Saturation")p={{"Drive",.2},{"Mix",.5}};
 else if(type=="Limiter")p={{"Ceiling",.95}};
 else p={{"Gain",.5}};
 }
 QString name()const override{return type;}
 std::vector<Parameter> parameters()const override{return p;}
 void setParameter(int i,double v)override{if(i>=0&&i<int(p.size()))p[size_t(i)].value=std::clamp(v,0.,1.);}
 void reset()override{std::fill(delay.begin(),delay.end(),0);cursor=0;frame=0;for(auto&r:room)std::fill(r.begin(),r.end(),0);std::fill(std::begin(damped),std::end(damped),0);env=0;gain=1;std::fill(std::begin(low),std::end(low),0);std::fill(std::begin(high),std::end(high),0);}
 QJsonObject save()const override{QJsonArray a;for(auto &v:p)a.append(v.value);return {{"type",type},{"bypass",bypass},{"values",a}};}
 void process(float*b,int n,const std::vector<MidiEvent>&,double,double)override{
 if(bypass)return;auto value=[&](int i){return float(p[size_t(i)].value);};
 float aLow=1-std::exp(-6.2831853f*200/float(rate)),aHigh=1-std::exp(-6.2831853f*4000/float(rate));
 for(int i=0;i<n;++i){float peak=std::max(std::abs(b[2*i]),std::abs(b[2*i+1]));env=std::max(peak,env*float(std::exp(-1/(rate*.08))));float dynamics=1;
 if(type=="Compressor"){float threshold=std::pow(10.f,(-48+value(0)*42)/20);float ratio=1+value(1)*11;dynamics=env>threshold?std::pow(threshold/env,1-1/ratio):1;dynamics*=std::pow(10.f,value(2)*18/20);}
 if(type=="Gate"){float threshold=std::pow(10.f,(-70+value(0)*60)/20);float target=env<threshold?0.f:1.f;gain+=(target-gain)*float(1-std::exp(-1/(rate*(target>gain?.002:.01+value(1)*.5))));dynamics=gain;}
 if(type=="Limiter"){float ceiling=.1f+value(0)*.88f;float target=peak>ceiling?ceiling/peak:1;gain=target<gain?target:gain+(target-gain)*.001f;dynamics=gain;}
 for(int c=0;c<2;++c){float x=b[2*i+c],y=x;
 if(type=="EQ"){low[c]+=aLow*(x-low[c]);high[c]+=aHigh*(x-high[c]);y=low[c]*std::pow(10.f,(value(0)-.5f)*24/20)+(high[c]-low[c])*std::pow(10.f,(value(1)-.5f)*24/20)+(x-high[c])*std::pow(10.f,(value(2)-.5f)*24/20);}
 else if(type=="Saturation"){float d=1+value(0)*15;float s=std::tanh(x*d)/std::tanh(d);y=x*(1-value(1))+s*value(1);}
 else if(type=="Reverb"){float sum=0;const double lengths[]={.0297,.0371,.0411,.0437};for(int k=0;k<4;++k){int j=c*4+k;auto&r=room[j];size_t offset=size_t(rate*(lengths[k]*(.5+value(0))+(c?.0007:0)));offset=std::clamp<size_t>(offset,1,r.size()-1);float tap=r[(frame+r.size()-offset)%r.size()];damped[j]+=.25f*(tap-damped[j]);r[frame%r.size()]=std::clamp(x*.25f+damped[j]*(.35f+value(1)*.55f),-4.f,4.f);sum+=tap;}for(int k=0;k<2;++k){auto&r=room[8+c*2+k];float tap=r[frame%r.size()];float diff=tap-sum*.5f;r[frame%r.size()]=sum+diff*.5f;sum=diff;}y=x*(1-value(2))+sum*value(2);}
 else if(type=="Delay"){size_t length=size_t(rate*(.02+value(0)*1.5))*2;length=std::max<size_t>(2,std::min(length,delay.size()-2));size_t read=(cursor+delay.size()-length+size_t(c))%delay.size();float tap=delay[read];delay[(cursor+size_t(c))%delay.size()]=std::clamp(x+tap*value(1)*.9f,-4.f,4.f);y=x*(1-value(2))+tap*value(2);}

 else if(type=="Gain")y=x*std::pow(10.f,(value(0)-.5f)*48/20);
 else y=x*dynamics;
 b[2*i+c]=std::isfinite(y)?y:0;
 }cursor=(cursor+2)%delay.size();++frame;
 }}
};
}
QStringList builtinNames(){return {"EQ","Compressor","Gate","Saturation","Delay","Reverb","Limiter","Gain"};}
std::shared_ptr<Effect> builtin(const QString&t,double sr){if(!builtinNames().contains(t))return {};return std::make_shared<Builtin>(t,sr);}
}
