#include "../firmware/AirMouse/PeerAddressCache.h"
#include <cassert>
#include <map>
#include <string>
#include <iostream>

std::map<std::string,ble_store_value_rpa_rec> flash;
int defaultCalls=0, deletes=0, openError=0, readError=0, deleteError=0, openHandles=0;
bool wrongSize=false;
// This sentinel represents the separately stored encryption keys. Cache
// reclamation must not invoke the default handler, which would unpair a host.
int bonds=1;
ble_addr_t addr(uint8_t n){return {1,{n,0,0,0,0,0}};}
bool equal(ble_addr_t a,ble_addr_t b){return !std::memcmp(&a,&b,sizeof(a));}
ble_store_value_rpa_rec record(uint8_t rpa,uint8_t identity){auto id=addr(identity);id.type=0;return {addr(rpa),id};}
int nvs_open(const char *ns,int mode,nvs_handle_t *h){
  assert(std::string(ns)=="nimble_bond" && mode==NVS_READONLY);
  if(openError)return openError;*h=1;++openHandles;return 0;
}
int nvs_get_blob(nvs_handle_t,const char *key,void *out,size_t *size){
  if(readError)return readError;
  auto it=flash.find(key);if(it==flash.end())return ESP_ERR_NVS_NOT_FOUND;
  assert(*size==sizeof(it->second));std::memcpy(out,&it->second,*size);
  if(wrongSize)--*size;return 0;
}
void nvs_close(nvs_handle_t){--openHandles;}
int ble_store_delete_rpa_rec(const ble_store_key_rpa_rec *key){
  // Model the pinned library defect: an all-zero/wildcard key does not match
  // ordinary addresses. Only an exact private address or identity works.
  if(deleteError)return deleteError;
  for(auto it=flash.begin();it!=flash.end();++it){
    if(equal(it->second.peer_rpa_addr,key->peer_rpa_addr)||equal(it->second.peer_addr,key->peer_rpa_addr)){
      flash.erase(it);++deletes;return 0;
    }
  }
  return BLE_HS_ENOENT;
}
int ble_store_util_status_rr(ble_store_status_event *e,void*){
  ++defaultCalls;if(e->event_code==BLE_STORE_EVENT_OVERFLOW)--bonds;return 0;
}
int overflow(ble_store_value_rpa_rec incoming){
  ble_store_value value{};value.rpa_rec=incoming;
  ble_store_status_event event{};event.event_code=BLE_STORE_EVENT_OVERFLOW;
  event.overflow={BLE_STORE_OBJ_TYPE_PEER_ADDR,&value};
  return air::PeerAddressCache::onStatus(&event,nullptr);
}
int main(){
  flash={{"rpa_rec_1",record(1,10)},{"rpa_rec_2",record(2,20)},{"rpa_rec_3",record(3,30)}};
  assert(overflow(record(4,20))==0); // Reclaim same host's obsolete alias first.
  assert(flash.count("rpa_rec_1")&& !flash.count("rpa_rec_2")&&flash.count("rpa_rec_3"));
  assert(bonds==1&&defaultCalls==0&&deletes==1);
  flash["rpa_rec_2"]=record(4,20);
  // Repeated forget/re-pair with rotating addresses must not delete bonds.
  for(uint8_t n=5;n<30;++n){
    assert(overflow(record(n,20))==0);flash["rpa_rec_2"]=record(n,20);
    assert(flash.size()==3&&bonds==1&&defaultCalls==0);
  }
  assert(overflow(record(40,40))==0); // Different host: free a cache slot only.
  assert(flash.size()==2&&bonds==1);
  assert(air::PeerAddressCache::clear()==0); // Also traverses holes in NVS indexes.
  assert(flash.empty()&&bonds==1&&openHandles==0);
  assert(overflow(record(1,10))==BLE_HS_ESTORE_FAIL); // No-progress retry is bounded.
  flash["rpa_rec_3"]=record(1,10);
  assert(overflow(record(1,10))==BLE_HS_ESTORE_FAIL); // Never delete the incoming alias.
  openError=123;assert(air::PeerAddressCache::clear()==BLE_HS_ESTORE_FAIL);openError=0;
  readError=123;assert(overflow(record(2,10))==BLE_HS_ESTORE_FAIL);readError=0;
  wrongSize=true;assert(air::PeerAddressCache::clear()==BLE_HS_ESTORE_FAIL);wrongSize=false;
  deleteError=27;assert(overflow(record(2,10))==27);assert(air::PeerAddressCache::clear()==27);deleteError=0;
  assert(flash.size()==1&&bonds==1&&defaultCalls==0&&openHandles==0);
  ble_store_status_event full{};full.event_code=BLE_STORE_EVENT_FULL;
  assert(air::PeerAddressCache::onStatus(&full,nullptr)==0&&defaultCalls==1);
  std::cout<<"Peer-address cache overflow/reset tests passed\n";
}
