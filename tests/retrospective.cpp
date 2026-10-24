#include "../src/audio/retrospective.h"
#include <cassert>
#include <thread>
#include <iostream>
int main() {
    rdj::Retrospective r;
    std::vector<float> out;
    assert(!r.capture(5, out));
    r.prepare(100);
    assert(!r.capture(5, out));
    float L[256], R[256];
    for (int block = 0; block < 40; ++block) {
        for (int i = 0; i < 256; ++i) { L[i] = float(block * 256 + i); R[i] = -L[i]; }
        r.push(L, R, 256);
    }
    assert(r.capture(30, out) && out.size() == 6000);
    for (size_t i = 0; i < out.size()/2; ++i) {
        assert(out[i*2] == float(10240 - 3000 + i));
        assert(out[i*2+1] == -out[i*2]);
    }
    assert(!r.capture(31, out));
    r.prepare(48000);
    std::atomic<bool> done{false};
    std::thread writer([&] {
        float a[256], b[256];
        for (int block = 0; block < 18000; ++block) {
            for (int i=0;i<256;++i) { a[i]=float(block*256+i); b[i]=-a[i]; }
            r.push(a,b,256);
        }
        done = true;
    });
    int captures = 0;
    while (!done) {
        if (!r.capture(5,out)) continue;
        for (size_t i=0;i<out.size()/2;++i) {
            assert(out[2*i+1] == -out[2*i]);
            if(i) assert(out[2*i] == out[2*i-2]+1);
        }
        ++captures;
    }
    writer.join();
    assert(r.capture(30,out));
    r.prepare(44100);
    assert(!r.capture(5,out));
    float a[2]={.5f,.75f}, b[2]={-.5f,-.75f};
    r.push(a,b,2);
    assert(r.capture(10,out) && out.size()==4 && out[2]==.75f);
    std::cout << "PASS: empty, partial, wrap, stereo, invalid duration, concurrent capture (" << captures << "), rate reset\n";
}
