#include "audio/loader.h"
#include <fstream>
#include <filesystem>
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void wav(const fs::path& path,int frames){
    std::ofstream f(path,std::ios::binary);auto u16=[&](uint16_t n){f.put(char(n&255));f.put(char(n>>8));};auto u32=[&](uint32_t n){for(int i=0;i<4;++i)f.put(char((n>>(i*8))&255));};
    f.write("RIFF",4);u32(36+frames*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);f.write("data",4);u32(frames*2);
    for(int i=0;i<frames;++i)u16(uint16_t(int16_t(3000*std::sin(i*6.283185307179586*220/48000))));
    if(!f)throw std::runtime_error("cannot write fixture");
}
int main(){const auto dir=fs::temp_directory_path()/("dj-drum-decode-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{
    fs::create_directories(dir);wav(dir/"short.wav",24000);wav(dir/"long.wav",48000*11);wav(dir/"tiny.wav",480);wav(dir/"ten.wav",48000*10);
    std::string err;auto shortSample=rdj::decodeDrumSample((dir/"short.wav").wstring(),err);if(!shortSample||shortSample->frames!=24000)throw std::runtime_error("short sample decoding");
    auto longSample=rdj::decodeDrumSample((dir/"long.wav").wstring(),err);if(longSample||err.find("10 segundos")==std::string::npos)throw std::runtime_error("duration cap");
    auto track=rdj::decodeFile((dir/"long.wav").wstring(),err);if(!track||track->frames!=48000*11)throw std::runtime_error("normal loader regression");
    auto tiny=rdj::decodeDrumSample((dir/"tiny.wav").wstring(),err);if(!tiny||tiny->frames!=480)throw std::runtime_error("10 ms percussion sample");
    auto tinyTrack=rdj::decodeFile((dir/"tiny.wav").wstring(),err);if(tinyTrack)throw std::runtime_error("normal track minimum changed");
    auto ten=rdj::decodeDrumSample((dir/"ten.wav").wstring(),err);if(!ten||ten->frames!=480000)throw std::runtime_error("exact 10 second boundary");
    auto missing=rdj::decodeDrumSample((dir/"missing.wav").wstring(),err);if(missing)throw std::runtime_error("missing file");
    fs::remove_all(dir);std::cout<<"PASS: actual WAV import, 10 ms percussion, duration cap, boundary, normal track loader and errors\n";
}catch(const std::exception& e){std::error_code ignored;fs::remove_all(dir,ignored);std::cerr<<e.what()<<"\n";return 1;}}
