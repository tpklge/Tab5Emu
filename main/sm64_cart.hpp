#pragma once
#include "cart.hpp"
#include "tab5_sm64.h"
#include "sm64_save.hpp"
#include "battery_store.hpp"
#include "content_paths.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
class N64Cart : public Cart {
public:
 explicit N64Cart(const Config&cfg):Cart(cfg){
  if(info_.content_hash!="17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91"){
   error_="Modo nativo requer a ROM US de SM64 usada neste build. Outros jogos: abra Tab5Emu.";return;
  }
  auto&store=tab5::content::primary_battery_store();
  if(!store.configure("sm64-native",info_.content_hash,512)){error_="Falha ao preparar save SM64.";return;}
  std::error_code ec;bool existing=false;
  for(const char*suffix:{"",".bak",".sha256",".sha256.bak"}){existing|=std::filesystem::exists(store.path()+suffix,ec);if(ec){error_="Falha ao consultar save SM64.";return;}}
  if(!store.load(battery_.data())){error_="Save nativo invalido; original preservado.";return;}
  if(!existing){
   tab5::content::BatteryStore legacy;
   if(!legacy.configure("n64",info_.content_hash,198656)){error_="Falha ao localizar save N64.";return;}
   bool old=false;
   for(const char*suffix:{"",".bak",".sha256",".sha256.bak"}){old|=std::filesystem::exists(legacy.path()+suffix,ec);if(ec)break;}
   if(ec){error_="Falha ao consultar save N64.";return;}
   if(old){
    std::vector<uint8_t>bytes(198656);
    if(!legacy.load(bytes.data())){error_="Save N64 invalido; importacao cancelada.";return;}
    imported_=tab5::sm64::import_eeprom(bytes,battery_);
    if(!imported_){error_="EEPROM SM64 sem assinatura valida; original preservado.";return;}
   }
  }
  frame_.resize(320*240*2);
  auto&emu=Tab5Emu::get();emu.native_size(320,240);emu.palette(nullptr);emu.audio_sample_rate(16000);handle_video_setting();
  if(FILE*cfg=fopen(tab5::content::resolve("config/sm64-performance.cfg").c_str(),"r")){
   unsigned w,h;char tail;
   if(fscanf(cfg,"%u %u %c",&w,&h,&tail)==2 && tab5_sm64_resolution(w,h)){width_=w;height_=h;}
   fclose(cfg);
  }
  tab5_sm64_callbacks cb{this,video,audio,save_battery};attempted_=true;
  if(!tab5_sm64_init(&cb,battery_.data())){struct tab5_sm64_stats s{};tab5_sm64_stats(&s);error_=s.error;return;}
  ready_=true;
 }
 ~N64Cart()override{
  if(ready_)(void)persist_before_exit();
  tab5::content::primary_battery_store().reset();
  if(attempted_)paludo_request_return(); // native engine globals require a fresh process
 }
 bool loaded()const override{return ready_;}
 std::string load_error()const override{return error_;}
 bool supports_resume()const override{return false;}
 void save()override{menu_->set_status("SM64 nativo salva o progresso no cartucho. Estados ainda indisponiveis.",true);}
 void load()override{save();}
 void reset()override{
  if(persist_before_exit()){paludo_request_return();running_=false;}
  else menu_->set_status("Falha ao salvar; reinicio cancelado.",true);
 }
 bool run()override{
  if(!Cart::run())return false;
  if(!ready_ || !error_.empty()){vTaskDelay(1);return true;}
  const auto s=Tab5Emu::get().gamepad_state();const auto axes=Tab5Emu::get().gamepad_axes();const auto usb=Tab5Emu::get().usb_gamepad_state();
  uint16_t b=(s.a?0x8000:0)|(s.b?0x4000:0)|(s.select?0x2000:0)|(s.start?0x1000:0)|(s.l?0x20:0)|(s.r?0x10:0)|(s.x?8:0)|(s.y?2:0)|(s.l2?4:0)|(s.r2?1:0);
  if(axes.rx<-10000)b|=2;
  if(axes.rx>10000)b|=1;
  if(axes.ry<-10000)b|=8;
  if(axes.ry>10000)b|=4;
  b|=(usb.up?0x800:0)|(usb.down?0x400:0)|(usb.left?0x200:0)|(usb.right?0x100:0);
  int x=(s.right?80:0)-(s.left?80:0),y=(s.up?80:0)-(s.down?80:0);
  if(abs(axes.lx)>4000 || abs(axes.ly)>4000){x=std::clamp(int(axes.lx)*80/32767,-80,80);y=std::clamp(-int(axes.ly)*80/32767,-80,80);}
  tab5_sm64_input(b,x,y);
  drain_audio();
  const auto begin=esp_timer_get_time();
  if(!deadline_ || deadline_<begin-4*33333)deadline_=begin;
  const unsigned due=std::clamp<int64_t>(1+(begin-deadline_)/33333,1,4);
  for(unsigned tick=0;tick<due;++tick){
   if(!tab5_sm64_tick(tick+1==due)){
    struct tab5_sm64_stats st{};tab5_sm64_stats(&st);error_=st.error;
    menu_->set_status(error_,true);Tab5Emu::get().request_menu();break;
   }
  }
  struct tab5_sm64_stats st{};tab5_sm64_stats(&st);{std::lock_guard<std::mutex>guard(mutex_);stats_=st;}
  const auto now=esp_timer_get_time();deadline_+=due*33333;
  if(deadline_>now)vTaskDelay(pdMS_TO_TICKS((deadline_-now+999)/1000));else vTaskDelay(1);
  return true;
 }
 uint32_t game_frames()const override{std::lock_guard<std::mutex>guard(mutex_);return stats_.rendered_frames;}
 std::string core_metrics_json()const override{
  std::lock_guard<std::mutex>guard(mutex_);
  return fmt::format("{{\"backend\":\"sm64-native-software\",\"ticks\":{},\"rendered_frames\":{},\"run_us\":{},\"render_us\":{},\"audio_us\":{},\"level\":{},\"allocated_bytes\":{},\"save_writes\":{},\"imported_n64_save\":{},\"render_width\":{},\"render_height\":{}}}",stats_.ticks,stats_.rendered_frames,stats_.run_us,stats_.render_us,stats_.audio_us,stats_.level,stats_.allocated_bytes,stats_.save_writes,imported_,width_,height_);
 }
protected:
 void post_menu()override{deadline_=0;pcm_used_=0;Cart::post_menu();}
 bool persist_before_exit()override{return !ready_ || tab5::content::primary_battery_store().flush(tab5_sm64_battery());}
 void set_original_video_setting()override{display_original(320,240);}
 void set_fit_video_setting()override{display_fit(320,240);}
 void set_fill_video_setting()override{display_fill();}
 std::pair<size_t,size_t>get_video_size()const override{return {320,240};}
 std::span<uint8_t>get_video_buffer()const override{return {const_cast<uint8_t*>(frame_.data()),frame_.size()};}
 std::string get_save_extension()const override{return "_sm64_native.sav";}
private:
 bool ready_=false,attempted_=false,imported_=false;unsigned frame_index_=0,width_=160,height_=120;int64_t deadline_=0;
 std::string error_;std::array<uint8_t,512>battery_{};std::vector<uint8_t>frame_;
 std::array<uint8_t,16384>pcm_{};size_t pcm_used_=0;uint32_t pcm_generation_=0;
 mutable std::mutex mutex_;struct tab5_sm64_stats stats_{};
 static bool save_battery(void*ctx,const void*p,size_t n){
  const bool ok=n==512 && tab5::content::primary_battery_store().flush(p);
  if(!ok){
   auto*self=static_cast<N64Cart*>(ctx);
   self->menu_->set_status("Falha ao salvar SM64. Progresso mantido na memoria; confira o SD antes de sair.",true);
   Tab5Emu::get().request_menu();
  }
  return ok;
 }
 static void video(void*ctx,const uint16_t*p,unsigned w,unsigned h){auto*self=static_cast<N64Cart*>(ctx);if(w!=320 || h!=240)return;
  memcpy(self->frame_.data(),p,320*240*2);auto&emu=Tab5Emu::get();auto*dst=self->frame_index_?emu.frame_buffer1():emu.frame_buffer0();
  if(dst && emu.frame_slot_free() && emu.video_frame_available(dst)){memcpy(dst,p,320*240*2);emu.push_frame(dst);self->frame_index_^=1;}
 }
 void drain_audio(){
  auto&emu=Tab5Emu::get();const auto generation=emu.audio_generation();
  if(generation!=pcm_generation_){pcm_used_=0;pcm_generation_=generation;}
  if(pcm_used_){const auto n=emu.play_audio(pcm_.data(),pcm_used_);pcm_used_-=n;memmove(pcm_.data(),pcm_.data()+n,pcm_used_);}
 }
 static void audio(void*ctx,const int16_t*p,size_t n,unsigned){
  auto*self=static_cast<N64Cart*>(ctx);self->drain_audio();
  if(n*4>self->pcm_.size()-self->pcm_used_){
   self->error_="Fila de audio SM64 cheia. Retorne ao Launcher para reiniciar.";
   self->menu_->set_status(self->error_,true);Tab5Emu::get().request_menu();return;
  }
  memcpy(self->pcm_.data()+self->pcm_used_,p,n*4);self->pcm_used_+=n*4;self->drain_audio();
 }
};
