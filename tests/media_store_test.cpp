#include "../firmware/AirMouse/MediaStore.h"
#include <cassert>
#include <vector>
#include <iostream>
static esp_partition_t partition{0x800000,0x1090000,ESP_PARTITION_TYPE_DATA};
static std::vector<uint8_t> flash(partition.size,0xff);
static bool failWrite=false;
const esp_partition_t *esp_partition_find_first(int type,int,const char *label){assert(type==ESP_PARTITION_TYPE_DATA&&!strcmp(label,"airmedia"));return &partition;}
int esp_partition_read(const esp_partition_t*,size_t offset,void*buffer,size_t n){
  if(offset+n>flash.size())return ESP_FAIL;memcpy(buffer,flash.data()+offset,n);return ESP_OK;
}
int esp_partition_write(const esp_partition_t*,size_t offset,const void*buffer,size_t n){
  if(failWrite||offset+n>flash.size())return ESP_FAIL;
  auto p=(const uint8_t*)buffer;
  for(size_t i=0;i<n;++i)if((flash[offset+i]&p[i])!=p[i])return ESP_FAIL;
  memcpy(flash.data()+offset,buffer,n);return ESP_OK;
}
int esp_partition_erase_range(const esp_partition_t*,size_t offset,size_t n){
  if(offset%4096||n%4096||offset+n>flash.size())return ESP_FAIL;
  memset(flash.data()+offset,0xff,n);return ESP_OK;
}
void erase(MediaStore &s){while(s.erasing())assert(s.eraseStep());}
int main(){
  const uint8_t red[]={65,77,70,49,205,0,251,0,1,0,0,0,100,0,4,0,0,0,255,200,0,248};
  std::vector<uint8_t> blue(red,red+sizeof(red));blue[20]=31;blue[21]=0;
  assert(air::validMedia(red,sizeof(red)));
  {MediaStore s;s.begin();assert(s.available&&!s.media);
   assert(!s.start(air::MaxMedia+1,0));assert(s.start(sizeof(red),air::crc32(red,sizeof(red))));
   assert(!s.start(sizeof(red),0));erase(s);
   assert(!s.append(1,red,sizeof(red)));assert(s.append(0,red,sizeof(red)));assert(s.commit());
   assert(!memcmp(s.media,red,sizeof(red)));}
  // Power loss after an incomplete upload keeps the previous bank.
  {MediaStore s;s.begin();assert(!memcmp(s.media,red,sizeof(red)));
   assert(s.start(blue.size(),air::crc32(blue.data(),blue.size())));erase(s);
   assert(s.append(0,blue.data(),8));assert(!s.commit());}
  {MediaStore s;s.begin();assert(!memcmp(s.media,red,sizeof(red)));
   assert(s.start(blue.size(),air::crc32(blue.data(),blue.size())));erase(s);
   assert(s.append(0,blue.data(),blue.size()));
   failWrite=true;assert(!s.commit());failWrite=false; // Failed commit-marker write preserves old data.
   assert(!memcmp(s.media,red,sizeof(red)));assert(s.commit());assert(!memcmp(s.media,blue.data(),blue.size()));}
  {MediaStore s;s.begin();assert(!memcmp(s.media,blue.data(),blue.size()));}
  // A damaged latest bank falls back to the older CRC-valid bank on boot.
  flash[0x400000+16+20]^=1;
  {MediaStore s;s.begin();assert(!memcmp(s.media,red,sizeof(red)));}
  // Refuse a mistakenly labelled app partition or a data partition at an unknown address.
  partition.type=0; {MediaStore s;s.begin();assert(!s.available&&!s.start(sizeof(red),0));}
  partition.type=ESP_PARTITION_TYPE_DATA;partition.address=0xc90000;
  {MediaStore s;s.begin();assert(!s.available&&!s.start(sizeof(red),0));}
  partition.address=0x1090000;partition.size=0xc80000;
  {MediaStore s;s.begin();assert(!s.available);}

  std::cout<<"Media storage: interrupted upload, commit failure, CRC fallback and dedicated data partition boundaries passed.\n";
}
