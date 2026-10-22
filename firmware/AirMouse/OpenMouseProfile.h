#pragma once
#include "Pointer.h"
// Independent implementation of the publicly documented moddo feature-report layout.
// See docs/OPENMOUSE.md for the upstream revision and experimental identity restrictions.
namespace air {
inline void moddoConfig(const Settings &c, uint8_t (&out)[11]) {
  memset(out,0,sizeof(out)); out[0]=125; // Actual firmware report cadence is 125 Hz, link scheduling may be slower.
  out[3]=c.dpiX; out[4]=c.dpiX>>8; out[5]=c.dpiY; out[6]=c.dpiY>>8;
}
inline bool applyModdo(const uint8_t *p, size_t n, Settings &c) {
  // A gyro has no lift-off distance. Reject unsupported writes instead of pretending to apply them.
  if(n!=11 || u16(p)!=125 || p[2]!=0 || p[7]!=0 || u16(p+8)!=0 || p[10]!=0)return false;
  Settings candidate=c;candidate.dpiX=u16(p+3);candidate.dpiY=u16(p+5);
  if(!candidate.valid())return false;c=candidate;return true;
}
class PointerScale {
  float x_=0,y_=0;
 public:
  void reset(){x_=y_=0;}
  Output apply(Output o,const Settings &c){
    x_+=o.x*(c.dpiX/1600.f);y_+=o.y*(c.dpiY/1600.f);
    o.x=int(x_);o.y=int(y_);x_-=o.x;y_-=o.y;return o;
  }
};
}
