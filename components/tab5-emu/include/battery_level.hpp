#pragma once
#include <cmath>
namespace tab5::power {
// Tab5's pack is two cells in series. This voltage estimate is not a fuel
// gauge; the UI also shows pack voltage and explicitly labels the estimate.
inline int estimated_percent(bool present,float pack_volts) {
  if(!present || !std::isfinite(pack_volts) || pack_volts<5.5f || pack_volts>9.0f)return -1;
  constexpr int mv[]={3300,3500,3600,3700,3750,3800,3900,4000,4100,4200};
  constexpr int pc[]={0,5,10,20,35,50,65,80,90,100};
  const int cell=static_cast<int>(pack_volts*500.0f);
  if(cell<=mv[0])return 0;
  for(unsigned i=1;i<10;++i)if(cell<=mv[i])
    return pc[i-1]+(cell-mv[i-1])*(pc[i]-pc[i-1])/(mv[i]-mv[i-1]);
  return 100;
}
}
