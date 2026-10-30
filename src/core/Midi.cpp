#include "Midi.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <stdexcept>
#include <tuple>
namespace bt {
namespace {
struct Reader { QByteArray b; int p=0; int end=0; explicit Reader(QByteArray data):b(std::move(data)),end(b.size()){}
 int byte(){if(p>=end)throw std::runtime_error("MIDI truncated");return static_cast<unsigned char>(b[p++]);}
 int be(int n){int v=0;while(n--)v=(v<<8)|byte();return v;}
 int vlq(){int v=0;for(int i=0;i<4;++i){int a=byte();v=(v<<7)|(a&127);if(!(a&128))return v;}throw std::runtime_error("Invalid MIDI variable length");}
 QByteArray take(int n){if(n<0||n>end-p)throw std::runtime_error("MIDI chunk exceeds file");auto a=b.mid(p,n);p+=n;return a;}
};
QString decode(const QByteArray &b){QString s=QString::fromUtf8(b);if(s.toUtf8()==b)return s;s.clear();for(unsigned char c:b){if(c<128)s+=QChar(c);else if(c>=0xa1&&c<=0xfb)s+=QChar(0x0e01+c-0xa1);else s+=QChar(' ');}return s;}
struct Raw { int tick=0,order=0,status=0,a=0,b=0,tempo=0; QString lyric; };
}
Song readSong(const QString &path){
 QFile f(path);if(!f.open(QFile::ReadOnly))throw std::runtime_error("Cannot read song");
 if(f.size()>32*1024*1024)throw std::runtime_error("MIDI file exceeds 32 MB");Reader r(f.readAll());
 if(r.take(4)!="MThd"||r.be(4)!=6)throw std::runtime_error("Not a standard MIDI file");
 int format=r.be(2),tracks=r.be(2),division=r.be(2);if(format>1||!tracks||!division||(division&0x8000))throw std::runtime_error("Only MIDI format 0/1 with PPQ is supported");
 QVector<Raw> raw;int order=0;
 for(int track=0;track<tracks;++track){if(r.take(4)!="MTrk")throw std::runtime_error("Missing MIDI track");int size=r.be(4);Reader t(r.take(size));int tick=0,running=0;
 while(t.p<t.end){int delta=t.vlq();if(delta>0x7fffffff-tick)throw std::runtime_error("MIDI time overflow");tick+=delta;int s=t.byte();if(s<128){if(!running)throw std::runtime_error("Invalid running status");--t.p;s=running;}
 Raw e;e.tick=tick;e.order=order++;
 if(s==0xff){int type=t.byte();auto data=t.take(t.vlq());if(type==0x51&&data.size()==3)e.tempo=(static_cast<unsigned char>(data[0])<<16)|(static_cast<unsigned char>(data[1])<<8)|static_cast<unsigned char>(data[2]);
 if(type==5||(type==1&&data.size()&&!data.startsWith('@')))e.lyric=decode(data);raw.push_back(e);running=0;if(type==0x2f)break;
 }else if(s==0xf0||s==0xf7){t.take(t.vlq());running=0;}else if(s>=0x80&&s<=0xef){running=s;e.status=s;e.a=t.byte();if((s&0xf0)!=0xc0&&(s&0xf0)!=0xd0)e.b=t.byte();if(e.a>127||e.b>127)throw std::runtime_error("Invalid MIDI data");raw.push_back(e);}else throw std::runtime_error("Unsupported MIDI status");
 if(raw.size()>2000000)throw std::runtime_error("Too many MIDI events");
 }}
 std::stable_sort(raw.begin(),raw.end(),[](const Raw&a,const Raw&b){return std::tie(a.tick,a.order)<std::tie(b.tick,b.order);});
 Song song;song.path=QFileInfo(path).absoluteFilePath();song.title=QFileInfo(path).completeBaseName();song.division=division;int last=0;double seconds=0,micro=500000;bool firstTempo=true;
 for(const auto&e:raw){seconds+=(e.tick-last)*micro/1000000.0/division;last=e.tick;if(e.tempo>0){micro=e.tempo;if(firstTempo){song.bpm=60000000.0/micro;firstTempo=false;}}if(e.status)song.events.push_back({seconds,e.tick,e.status,e.a,e.b});if(!e.lyric.isEmpty())song.lyrics.push_back({seconds,e.lyric});song.duration=seconds;}
 song.duration+=1;
 // NCN companion files: lyrics use four header lines, CUR stores 1/24-quarter positions.
 QFileInfo info(path);QString normalized=QDir::fromNativeSeparators(info.absoluteFilePath());int songFolder=normalized.lastIndexOf("/Song/",-1,Qt::CaseInsensitive);if(songFolder>=0){QString root=normalized.left(songFolder);QString relative=normalized.mid(songFolder+6);QString base=relative.left(relative.size()-info.suffix().size()-1);QString lyrPath,curPath;
 for(const QString &ext:{QString(".lyr"),QString(".LYR"),QString(".Lyr")})if(QFile::exists(root+"/Lyrics/"+base+ext))lyrPath=root+"/Lyrics/"+base+ext;
 for(const QString &ext:{QString(".cur"),QString(".CUR"),QString(".Cur")})if(QFile::exists(root+"/Cursor/"+base+ext))curPath=root+"/Cursor/"+base+ext;
 QFile l(lyrPath),c(curPath);if(l.open(QFile::ReadOnly)&&c.open(QFile::ReadOnly)){auto lines=decode(l.readAll()).split('\n');if(lines.size()>=4){song.title=lines[0].trimmed();song.artist=lines[1].trimmed();QString text=lines.mid(4).join("\n");auto curs=c.readAll();song.lyrics.clear();int charIndex=0;
 auto tickTime=[&](int target){double time=0,tempo=500000;int prev=0;for(const auto&e:raw){if(e.tick>target)break;if(e.tempo>0){time+=(e.tick-prev)*tempo/1e6/division;prev=e.tick;tempo=e.tempo;}}return time+(target-prev)*tempo/1e6/division;};
 for(int i=0;i+1<curs.size()&&charIndex<text.size();i+=2){int unit=static_cast<unsigned char>(curs[i])|(static_cast<unsigned char>(curs[i+1])<<8);song.lyrics.push_back({tickTime(unit*division/24),text.mid(charIndex++,1)});}
 }}}
 return song;
}
int partFor(int channel,int note){if(channel!=9)return channel<9?channel:channel-1;if(note==35||note==36)return 15;if(note==38||note==40)return 16;if(note==42||note==44)return 17;if(note==46)return 18;if(note==41||note==43||note==45||note==47||note==48||note==50)return 19;if(note==49||note==51||note==52||note==55||note==57||note==59)return 20;if(note==39||note==37)return 21;return 22;}
QString partName(int p){static const char* drums[]={"Kick","Snare","Hi-hat • Closed","Hi-hat • Open","Toms","Cymbals","Clap / Rim","Percussion"};if(p==23)return "MASTER";if(p>=15)return QString::fromUtf8(drums[p-15]);return QString("Instrument %1 • MIDI %2").arg(p+1).arg(p<9?p+1:p+2);}
}
