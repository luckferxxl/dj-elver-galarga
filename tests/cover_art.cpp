#include "core/tags.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
static void setVdjDir(const std::wstring& v) { SetEnvironmentVariableW(L"RDJ_VDJ_DIR", v.empty() ? nullptr : v.c_str()); }
#else
#include <cstdlib>
static void setVdjDir(const std::wstring& v) { if (v.empty()) unsetenv("RDJ_VDJ_DIR"); else setenv("RDJ_VDJ_DIR", std::filesystem::path(v).string().c_str(), 1); }
#endif
#include <stdexcept>
#include <chrono>
using Bytes=std::vector<uint8_t>;
void be(Bytes& b,uint32_t n) {for(int i=3;i>=0;--i)b.push_back((n>>(8*i))&255);}
void le(Bytes& b,uint32_t n) {for(int i=0;i<4;++i)b.push_back((n>>(8*i))&255);}
void sync(Bytes& b,uint32_t n) {for(int i=3;i>=0;--i)b.push_back((n>>(7*i))&127);}
void str(Bytes& b,const char* s) {while(*s)b.push_back(*s++);}
void put(const std::filesystem::path& f,const Bytes& b) {std::ofstream o(f,std::ios::binary);o.write((const char*)b.data(),b.size());}
Bytes picture={0x89,'P','N','G',13,10,26,10,0xff,0xe1,1,2,3,4,5,6,7};
Bytes id3(int version,int flags=0,bool unsync=false) {
 Bytes apic={0};str(apic,"image/png");apic.push_back(0);apic.push_back(3);apic.push_back(0);apic.insert(apic.end(),picture.begin(),picture.end());
 if(flags&0x40)apic.insert(apic.begin(),7);
 if(unsync) {Bytes b;for(size_t i=0;i<apic.size();++i) {b.push_back(apic[i]);if(apic[i]==255)b.push_back(0);}apic=b;}
 Bytes frame;str(frame,"APIC");if(version==4)sync(frame,apic.size());else be(frame,apic.size());frame.push_back(0);frame.push_back(flags);frame.insert(frame.end(),apic.begin(),apic.end());
 Bytes out;str(out,"ID3");out.push_back(version);out.push_back(0);out.push_back(unsync?0x80:0);sync(out,frame.size());out.insert(out.end(),frame.begin(),frame.end());return out;
}
int main() {auto dir=std::filesystem::temp_directory_path()/("rdj-cover-regression-06-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(dir);int tests=0;
 auto check=[&](bool ok,const char* s){++tests;if(!ok)throw std::runtime_error(s);};
 try {
 auto mp3=dir/"tag.mp3";
 put(mp3,id3(3));check(rdj::readCoverArt(mp3.wstring())==picture,"ID3v2.3 APIC");
 put(mp3,id3(4,0x40,true));check(rdj::readCoverArt(mp3.wstring())==picture,"ID3v2.4 group + tag unsync");
 Bytes pic={0,'P','N','G',3,0};pic.insert(pic.end(),picture.begin(),picture.end());Bytes fr;str(fr,"PIC");fr.push_back(0);fr.push_back(0);fr.push_back(pic.size());fr.insert(fr.end(),pic.begin(),pic.end());Bytes v2={'I','D','3',2,0,0};sync(v2,fr.size());v2.insert(v2.end(),fr.begin(),fr.end());put(mp3,v2);check(rdj::readCoverArt(mp3.wstring())==picture,"ID3v2.2 PIC");
 Bytes chunk;str(chunk,"id3 ");auto id=id3(3);le(chunk,id.size());chunk.insert(chunk.end(),id.begin(),id.end());if(id.size()%2)chunk.push_back(0);
 Bytes wav;str(wav,"RIFF");le(wav,4+chunk.size());str(wav,"WAVE");wav.insert(wav.end(),chunk.begin(),chunk.end());auto wave=dir/"tag.wav";put(wave,wav);check(rdj::readCoverArt(wave.wstring())==picture,"RIFF ID3 chunk");
 Bytes blk;be(blk,3);be(blk,9);str(blk,"image/png");be(blk,0);be(blk,1);be(blk,1);be(blk,24);be(blk,0);be(blk,picture.size());blk.insert(blk.end(),picture.begin(),picture.end());Bytes fl={'f','L','a','C',0x86,0,0,(uint8_t)blk.size()};fl.insert(fl.end(),blk.begin(),blk.end());auto flac=dir/"tag.flac";put(flac,fl);check(rdj::readCoverArt(flac.wstring())==picture,"FLAC front picture");
 auto side=dir/"side.mp3";put(side,{});put(dir/"side.jpeg",picture);check(rdj::readCoverArt(side.wstring())==picture,"same-name JPEG");
 auto fallback=dir/"fallback.ogg";put(fallback,{});put(dir/"folder.png",picture);check(rdj::readCoverArt(fallback.wstring())==picture,"folder fallback");std::filesystem::remove(dir/"folder.png");
 auto good=id3(4);for(size_t n=0;n<good.size();++n){Bytes b(good.begin(),good.begin()+n);put(mp3,b);(void)rdj::readCoverArt(mp3.wstring());}check(true,"truncated frames bounded");
 // 0.6.1: etiquetas reales mal formadas que dejaban la carátula sin mostrar.
 auto apicBody=[&]{Bytes a={0};str(a,"image/png");a.push_back(0);a.push_back(3);a.push_back(0);a.insert(a.end(),picture.begin(),picture.end());return a;};
 auto tagWith=[&](int ver,const Bytes& frames){Bytes out;str(out,"ID3");out.push_back(ver);out.push_back(0);out.push_back(0);Bytes body=frames;body.resize(body.size()+64,0);sync(out,body.size());out.insert(out.end(),body.begin(),body.end());return out;};
 { // v2.4 con tamaños de marco escritos como en v2.3 (no synchsafe); el APIC pasa de 127 bytes
   Bytes big=picture;big.resize(300,7);Bytes a={0};str(a,"image/png");a.push_back(0);a.push_back(3);a.push_back(0);a.insert(a.end(),big.begin(),big.end());
   Bytes t2={3};str(t2,"Titulo");Bytes fr;str(fr,"TIT2");be(fr,t2.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),t2.begin(),t2.end());
   str(fr,"APIC");be(fr,a.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),a.begin(),a.end());
   put(mp3,tagWith(4,fr));check(rdj::readCoverArt(mp3.wstring())==big,"ID3v2.4 non-synchsafe frame sizes"); }
 { Bytes fr;str(fr,"TXXX");be(fr,0);fr.push_back(0);fr.push_back(0);auto a=apicBody();str(fr,"APIC");be(fr,a.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),a.begin(),a.end());
   put(mp3,tagWith(3,fr));check(rdj::readCoverArt(mp3.wstring())==picture,"zero-size frame before APIC"); }
 { Bytes t2={0};str(t2,"x");Bytes fr;str(fr,"TIT2");be(fr,t2.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),t2.begin(),t2.end());
   Bytes two=tagWith(3,fr);auto second=id3(3);two.insert(two.end(),second.begin(),second.end());
   put(mp3,two);check(rdj::readCoverArt(mp3.wstring())==picture,"cover in a second ID3 tag"); }
 { // APEv2 al final (foobar2000)
   Bytes item;le(item,10+picture.size());le(item,2);str(item,"Cover Art (Front)");item.push_back(0);str(item,"cover.jpg");item.push_back(0);item.insert(item.end(),picture.begin(),picture.end());
   Bytes ape;ape.insert(ape.end(),item.begin(),item.end());str(ape,"APETAGEX");le(ape,2000);le(ape,item.size()+32);le(ape,1);le(ape,0);for(int i=0;i<8;++i)ape.push_back(0);
   Bytes f2={0xFF,0xFB,0x90,0x64};f2.resize(400,0);f2.insert(f2.end(),ape.begin(),ape.end());put(mp3,f2);check(rdj::readCoverArt(mp3.wstring())==picture,"APEv2 cover"); }
 { // FLAC con una etiqueta ID3 delante
   Bytes blk;be(blk,3);be(blk,9);str(blk,"image/png");be(blk,0);be(blk,1);be(blk,1);be(blk,24);be(blk,0);be(blk,picture.size());blk.insert(blk.end(),picture.begin(),picture.end());
   Bytes t2={0};str(t2,"x");Bytes fr;str(fr,"TIT2");be(fr,t2.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),t2.begin(),t2.end());
   Bytes fl=tagWith(3,fr);Bytes body={'f','L','a','C',0x86,0,0,(uint8_t)blk.size()};body.insert(body.end(),blk.begin(),blk.end());fl.insert(fl.end(),body.begin(),body.end());
   auto flac2=dir/"id3.flac";put(flac2,fl);check(rdj::readCoverArt(flac2.wstring())==picture,"FLAC with leading ID3"); }
 { // Windows Media Player: AlbumArt_{GUID}_Large.jpg en la carpeta
   auto sub=dir/"wmp";std::filesystem::create_directories(sub);put(sub/"a.mp3",{});put(sub/"AlbumArt_{01234567-89AB-CDEF-0123-456789ABCDEF}_Large.jpg",picture);put(sub/"AlbumArtSmall.jpg",{1,2,3});
   check(rdj::readCoverArt((sub/"a.mp3").wstring())==picture,"WMP AlbumArt Large"); }
 { // Una sola imagen en la carpeta (cualquier nombre)
   auto sub=dir/"one";std::filesystem::create_directories(sub);put(sub/"a.mp3",{});put(sub/"Mi disco.png",picture);
   std::string src;check(rdj::readCoverArt((sub/"a.mp3").wstring(),&src)==picture && src.find("carpeta")!=std::string::npos,"single image in folder"); }
 { // 0.7: "pista.mp3.jpg" (VirtualDJ / reproductores) junto a la pista
   auto sub=dir/"full";std::filesystem::create_directories(sub);put(sub/"tema.mp3",{});put(sub/"otra.mp3",{});put(sub/"tema.mp3.jpg",picture);put(sub/"otra.jpg",Bytes{9,9,9});
   check(rdj::readCoverArt((sub/"tema.mp3").wstring())==picture,"track.mp3.jpg sidecar"); }
 { // 0.7: caché de carátulas de VirtualDJ (Cache\Covers), emparejada por nombre
   auto vdj=dir/"VirtualDJ";std::filesystem::create_directories(vdj/"Cache"/"Covers");put(vdj/"Cache"/"Covers"/"NANDU Sound - Noche de Verano!.mp3.jpg",picture);
   setVdjDir(vdj.wstring());
   auto sub=dir/"vdjmusic";std::filesystem::create_directories(sub);put(sub/"nandu sound - noche de verano.mp3",{});put(sub/"x1.mp3",{});put(sub/"x2.mp3",{});
   std::string src;check(rdj::readCoverArt((sub/"nandu sound - noche de verano.mp3").wstring(),&src)==picture && src.find("VirtualDJ")!=std::string::npos,"VirtualDJ cover cache");
   check(rdj::readCoverArt((sub/"x1.mp3").wstring()).empty(),"VirtualDJ cache: no false match");
   setVdjDir(L""); }
 { // 0.7: género y valoración (TCON con número ID3v1, POPM)
   Bytes g={0};str(g,"(18)");Bytes fr;str(fr,"TCON");be(fr,g.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),g.begin(),g.end());
   Bytes pm;str(pm,"Windows Media Player 9 Series");pm.push_back(0);pm.push_back(196);be(pm,0);str(fr,"POPM");be(fr,pm.size());fr.push_back(0);fr.push_back(0);fr.insert(fr.end(),pm.begin(),pm.end());
   put(mp3,tagWith(3,fr));auto ti=rdj::readTags(mp3.wstring());check(ti.genre=="Techno"&&ti.rating==4,"TCON (18) + POPM 196");
   Bytes g2={3};str(g2,"Tech House");Bytes fr2;str(fr2,"TCON");be(fr2,g2.size());fr2.push_back(0);fr2.push_back(0);fr2.insert(fr2.end(),g2.begin(),g2.end());
   put(mp3,tagWith(3,fr2));check(rdj::readTags(mp3.wstring()).genre=="Tech House","TCON text"); }
 check(std::string(rdj::imageFormatName(picture))=="PNG","image format name");
 std::filesystem::remove_all(dir);std::cout<<"PASS "<<tests<<" cover cases\n";return 0;
 } catch(const std::exception& e){std::cerr<<e.what()<<"\n";std::filesystem::remove_all(dir);return 1;}
}
