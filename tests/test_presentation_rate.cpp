#include "presentation_rate.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 tab5::PresentationRate rate;
 for(unsigned i=0;i<=70;++i)rate.present(uint64_t(i)*33333);
 assert(std::abs(rate.fps()-30)<.01f);
 const float before=rate.fps();rate.reset(false);
 assert(rate.fps()==before);
 rate.present(20000000); // A long pause is not part of the resumed window.
 for(unsigned i=1;i<=40;++i)rate.present(20000000+uint64_t(i)*33333);
 assert(std::abs(rate.fps()-30)<.01f);
 rate.reset();assert(rate.fps()==0);
 // A 5 ms copy every 100 ms is 10 displayed FPS, not 200 FPS.
 for(unsigned i=0;i<=12;++i)rate.present(uint64_t(i)*100000);
 assert(std::abs(rate.fps()-10)<.01f);
 puts("Presentation rate: wall-clock cadence, pause/resume and new-game reset passed");
}
