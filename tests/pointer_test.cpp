#include "../firmware/AirMouse/Pointer.h"
#include "../firmware/AirMouse/PairingButton.h"
#include "../firmware/AirMouse/TransferClock.h"
#include "../firmware/AirMouse/MediaFormat.h"
#include "../firmware/AirMouse/OpenMouseProfile.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
using namespace air;
Output release(Gesture &g, uint32_t at, const Settings &c) { g.update({true,false,0,0},at,c); return g.update({true,false,0,0},at+25,c); }
void put16(std::vector<uint8_t>&v,uint16_t n){v.push_back(n);v.push_back(n>>8);}
void put32(std::vector<uint8_t>&v,uint32_t n){put16(v,n);put16(v,n>>16);}
int main(int argc, char **argv) {
  Settings c; assert(c.valid()); assert(sizeof(c)==36);
  uint8_t profile[11];moddoConfig(c,profile);assert(u16(profile)==125&&u16(profile+3)==1600);
  profile[3]=0x80;profile[4]=0x0c;assert(applyModdo(profile,11,c));assert(c.dpiX==3200);
  auto before=c;profile[0]=250;assert(!applyModdo(profile,11,c));assert(c.dpiX==before.dpiX);
  profile[0]=125;profile[2]=1;assert(!applyModdo(profile,11,c));c=Settings();
  PointerScale scale;Output scaled;scaled.x=100;scaled.y=100;c.dpiX=3200;c.dpiY=800;
  scaled=scale.apply(scaled,c);assert(scaled.x==200&&scaled.y==50);c=Settings();
  auto bad=c; bad.gyroGain=std::numeric_limits<float>::quiet_NaN(); assert(!bad.valid());
  bad=c; bad.axisX=bad.axisY; assert(!bad.valid());
  Gesture g; g.update({true,true,200,250},0,c); assert(release(g,100,c).click==1);
  assert(release(g,200,c).click==0); // No double emission.
  g.update({true,true,200,250},1000,c); assert(g.update({true,true,203,252},1600,c).click==2);
  assert(release(g,1700,c).click==0);
  g.update({true,true,200,250},2000,c); auto o=g.update({true,true,240,270},2040,c);
  assert(o.x==56 && o.y==28 && !o.click); assert(release(g,2100,c).click==0);
  // Every edge direction emits scroll without cursor movement.
  struct Swipe { int x1,y1,x2,y2,wheel,pan; };
  for(auto s: {Swipe{20,250,390,250,0,12},Swipe{390,250,20,250,0,-12},Swipe{200,20,200,480,-12,0},Swipe{200,480,200,20,12,0}}) {
    g.update({true,true,s.x1,s.y1},3000,c); o=g.update({true,true,s.x2,s.y2},3200,c); assert(!o.x&&!o.y);
    o=release(g,3210,c); assert(o.wheel==s.wheel&&o.pan==s.pan&&!o.click);
  }
  c.flags|=4; g.update({true,true,20,250},0,c); g.update({true,true,390,250},200,c); assert(release(g,210,c).pan==-12); c.flags&=~4;
  // Slow edge-to-edge movement becomes pointer input, never a late swipe.
  g.update({true,true,20,250},0,c); o=g.update({true,true,390,250},500,c); assert(o.x>0); o=release(g,600,c); assert(!o.pan&&!o.click);
  // Aborted/partial edge swipe becomes pointer movement, not a click.
  g.update({true,true,20,250},0,c); g.update({true,true,100,250},100,c); o=release(g,150,c); assert(o.x>0&&!o.pan&&!o.click);
  // A dropped touch sample cannot fabricate a tap or a hold.
  g.update({true,true,200,250},0,c); g.update({false,false,0,0},150,c); assert(!g.active()); assert(!release(g,170,c).click);
  // Release debounce tolerates a short count=0 sample.
  g.update({true,true,200,250},0,c); g.update({true,false,0,0},80,c); g.update({true,true,200,250},88,c); assert(release(g,120,c).click==1);
  // Timestamp wraparound remains valid.
  g.update({true,true,200,250},0xfffffff0,c); assert(release(g,50,c).click==1);
  // Flash writes finish after the loop timestamp. This used to underflow and
  // abort the transfer immediately after an acknowledged chunk.
  TransferClock transfer; assert(!transfer.expired(100000));
  transfer.touch(1005); assert(!transfer.expired(1000)); assert(!transfer.expired(1006));
  assert(!transfer.expired(31005)); assert(transfer.expired(31006));
  transfer.clear(); assert(!transfer.expired(999999));
  transfer.touch(0xfffffff0); assert(!transfer.expired(100)); assert(transfer.expired(31000));
  transfer.touch(0); assert(transfer.expired(30001)); // Zero is a valid timestamp.
  // Legacy saved settings and writes cannot enable motion; retain other preferences.
  c.flags=7; c.touchGain=2.5f; c.disableMotion(); assert(c.flags==6&&c.touchGain==2.5f);
  assert((Settings().flags&1)==0);
  // Debounced short press cancels; a long press fires only after stable release.
  PairingButton button;
  button.update(false,0); button.update(false,30);
  button.update(true,100); button.update(true,130); assert(button.down());
  button.update(false,1000); assert(!button.update(false,1030)&&!button.down());
  button.update(true,2000); button.update(true,2030);
  assert(!button.update(true,5029)&&!button.armed());
  assert(!button.update(true,5030)&&button.armed());
  assert(!button.update(true,9000)); // Holding cannot reboot into download mode.
  button.update(false,9010); button.update(true,9020); assert(!button.update(true,9050));
  button.update(false,9100); assert(!button.update(false,9129));
  assert(button.update(false,9130)); assert(!button.update(false,9200));
  // An already-held startup button is ignored until released and pressed anew.
  PairingButton held; held.update(true,0); held.update(true,6000);
  held.update(false,6100); assert(!held.update(false,6130));
  held.update(true,6200); held.update(true,6230); held.update(true,9230);
  held.update(false,9240); assert(held.update(false,9270));
  PairingButton wrap; wrap.update(false,0xfffff000); wrap.update(false,0xfffff020);
  wrap.update(true,0xfffffff0); wrap.update(true,0x10); wrap.update(true,0x10+3000);
  wrap.update(false,0x10+3010); assert(wrap.update(false,0x10+3040));
  assert(crc32((const uint8_t*)"123456789",9)==0xcbf43926);
  std::vector<uint8_t> media={'A','M','F','1'};put16(media,205);put16(media,251);put16(media,1);put16(media,0);
  put16(media,100);put32(media,4);put16(media,205*251);put16(media,0xf800);assert(validMedia(media.data(),media.size()));
  for(size_t n=0;n<media.size();++n)assert(!validMedia(media.data(),n));
  auto broken=media;broken.push_back(0);assert(!validMedia(broken.data(),broken.size()));
  broken=media;broken[18]=0;broken[19]=0;assert(!validMedia(broken.data(),broken.size()));
  // Deterministic malformed-format fuzzing under ASan/UBSan.
  uint32_t rng=19;for(int i=0;i<10000;++i){broken=media;rng=rng*1664525+1013904223;broken[rng%broken.size()]^=uint8_t(rng>>24);validMedia(broken.data(),broken.size());}
  if(argc==2){FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);std::vector<uint8_t>v(n);assert(fread(v.data(),1,n,f)==n);fclose(f);assert(validMedia(v.data(),v.size()));}
  std::cout<<"Settings, gestures, pairing button, CRC and media validation passed.\n";
}
