// Run the real pinned feeder against a deterministic FreeRTOS clock/stream.
#include "i2s_tx_feed.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
struct TestStream { std::vector<uint8_t> bytes; };
static TickType_t now=0, arrival=UINT32_MAX;
static TestStream stream;
static size_t incoming=0;
static unsigned stale=0;
static void deliver() {
  if(now>=arrival){stream.bytes.insert(stream.bytes.end(),incoming,42);arrival=UINT32_MAX;}
}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s){deliver();return s->bytes.size();}
size_t xStreamBufferReceive(StreamBufferHandle_t s,void *out,size_t n,TickType_t){
  n=std::min(n,s->bytes.size());std::memcpy(out,s->bytes.data(),n);
  s->bytes.erase(s->bytes.begin(),s->bytes.begin()+n);return n;
}
int xStreamBufferReset(StreamBufferHandle_t s){s->bytes.clear();return pdTRUE;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return &stream;}
void vTaskDelay(TickType_t delay){now+=delay;deliver();}
void xTaskNotifyGive(TaskHandle_t){}
void vTaskSetTimeOutState(TimeOut_t *t){t->last=now;}
int xTaskCheckForTimeOut(TimeOut_t *t,TickType_t *remaining){
  const auto elapsed=now-t->last;t->last=now;
  if(elapsed>=*remaining){*remaining=0;return pdTRUE;}
  *remaining-=elapsed;return pdFALSE;
}
uint32_t ulTaskNotifyTake(int,TickType_t wait){
  if(stale){--stale;now+=4;deliver();return 1;}
  now=std::min<uint64_t>(uint64_t(now)+wait,arrival);deliver();return 1;
}
int main(){
  using namespace espp::i2s_tx_feed;
  static_assert(WHOLE_BLOCK_WAIT_MS==16);
  static_assert(WHOLE_BLOCK_WAIT_MS*60<1000);
  static_assert(ring_outlasts_wait(DMA_DESC_NUM,DMA_FRAME_NUM,48000,60));
  Feeder feeder;feeder.stream=&stream;
  std::array<uint8_t,3200> output{};bool misaligned=false;
  // Completing a frame after 12 ms must not become a padded partial block.
  stream.bytes.assign(1600,42);arrival=12;incoming=1600;
  assert(feeder.fill_block(output.data(),output.size(),misaligned)==3200);
  assert(now==12 && !misaligned);
  for(auto byte:output)assert(byte==42);
  // A stale notification must not renew the total bounded wait.
  now=0;arrival=100;incoming=3200;stale=3;stream.bytes.clear();feeder.was_flowing=true;
  assert(feeder.fill_block(output.data(),output.size(),misaligned)==0);
  assert(now==16);for(auto byte:output)assert(byte==0);
  // Idle streams do not wait; late streams still emit one complete block.
  now=0;arrival=UINT32_MAX;feeder.was_flowing=false;
  assert(feeder.fill_block(output.data(),output.size(),misaligned)==0 && now==0);
  stream.bytes.assign(400,9);feeder.was_flowing=true;
  assert(feeder.fill_block(output.data(),output.size(),misaligned)==400 && now==16);
  assert(output[399]==9 && output[400]==0 && output.back()==0);
  std::cout<<"Real feeder: 12ms producer jitter absorbed; stale notifications bounded; idle/late streams padded safely.\n";
}
