#pragma once
#if defined(TAB5_SM64_NATIVE)
#include "sm64_cart.hpp"
#else
#include "cart.hpp"
#include <algorithm>
#if defined(ENABLE_N64)
#include "n64_console.h"
#include "battery_store.hpp"
#include "content_paths.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
class N64Cart : public Cart {
public:
  explicit N64Cart(const Config&config):Cart(config){
#if defined(ENABLE_N64)
    file_=fopen(get_rom_filename().c_str(),"rb");if(!file_){error_="Falha ao abrir ROM N64 no SD.";return;}
    if(fseek(file_,0,SEEK_END)){error_="Falha ao medir ROM N64.";return;}
    const long length=ftell(file_);rewind(file_);
    uint8_t header[4];if(length<4096 || length>64*1024*1024 || length%4 || fread(header,1,4,file_)!=4){error_="ROM N64 truncada ou acima de 64 MiB.";return;}
    if(!memcmp(header,"\x80\x37\x12\x40",4))order_=0;
    else if(!memcmp(header,"\x37\x80\x40\x12",4))order_=1;
    else if(!memcmp(header,"\x40\x12\x37\x80",4))order_=2;
    else{error_="Cabecalho N64 invalido (.z64/.v64/.n64).";return;}
    tab5_n64_callbacks cb{};cb.context=this;cb.rom_read=read_rom;cb.video=video;cb.audio=audio;cb.poll=poll;
    machine_=tab5_n64_create(length,&cb);
    if(!machine_){error_="PSRAM insuficiente para RDRAM N64.";return;}
    auto&store=tab5::content::primary_battery_store();
    if(!store.configure("n64",info_.content_hash,TAB5_N64_BATTERY) || !store.load(tab5_n64_battery(machine_))){error_="Save N64 invalido; original preservado.";return;}
    options_={false,false,1};
#ifdef TAB5_N64_FAST_CPU
    options_.fast_cpu=true;
#endif
#ifdef TAB5_N64_AUDIO_HLE
    options_.audio_hle=true;
#endif
    if(FILE*cfg=fopen(tab5::content::resolve("config/n64-performance.cfg").c_str(),"r")){
      unsigned fast,audio,step;char tail;
      if(fscanf(cfg,"%u %u %u %c",&fast,&audio,&step,&tail)==3 && fast<=1 && audio<=1 && (step==1 || step==2))options_={bool(fast),bool(audio),step};
      fclose(cfg);
    }
    if(!tab5_n64_set_options(machine_,&options_)){error_="Configuracao N64 nao disponivel neste build.";return;}
    if(!tab5_n64_boot(machine_)){tab5_n64_get_stats(machine_,&stats_);error_=stats_.error;return;}
    frame_.resize(320*240*2);auto&emu=Tab5Emu::get();emu.native_size(320,240);emu.palette(nullptr);emu.audio_sample_rate(48000);handle_video_setting();ready_=true;
#endif
  }
  ~N64Cart() override {
#if defined(ENABLE_N64)
    if(ready_)(void)persist_before_exit();
    tab5_n64_destroy(machine_);tab5::content::primary_battery_store().reset();if(file_)fclose(file_);
    Tab5Emu::get().clear_audio();Tab5Emu::get().audio_sample_rate(48000);
#endif
  }
  bool loaded() const override{return ready_;}
  std::string load_error() const override{return error_.empty()?"N64 nao disponivel neste perfil.":error_;}
  bool supports_resume() const override{return false;}
  void save() override {menu_->set_status("Estados N64 ainda nao disponiveis. Save do cartucho e preservado na saida.",true);}
  void load() override {menu_->set_status("Estados N64 ainda nao disponiveis neste perfil.",true);}
  void reset() override {
#if defined(ENABLE_N64)
    if(!persist_before_exit()){menu_->set_status("Falha ao gravar save N64; reset cancelado.",true);return;}
    if(machine_ && persist_before_exit()){
      tab5_n64_destroy(machine_);machine_=nullptr;
      // A reset is a clean cartridge boot with the same persisted battery.
      tab5_n64_callbacks cb{};cb.context=this;cb.rom_read=read_rom;cb.video=video;cb.audio=audio;cb.poll=poll;
      fseek(file_,0,SEEK_END);const auto bytes=ftell(file_);machine_=tab5_n64_create(bytes,&cb);
      ready_=machine_ && tab5::content::primary_battery_store().load(tab5_n64_battery(machine_)) && tab5_n64_set_options(machine_,&options_) && tab5_n64_boot(machine_);
      if(ready_)error_.clear();
    }
#endif
  }
  bool run() override {
#if defined(ENABLE_N64)
    if(ready_ && error_.empty()){
      const auto s=Tab5Emu::get().gamepad_state();
      uint16_t b=(s.a?0x8000:0)|(s.b?0x4000:0)|(s.select?0x2000:0)|(s.start?0x1000:0)|
        (s.l?0x20:0)|(s.r?0x10:0)|(s.x?8:0)|(s.y?2:0)|(s.l2?4:0)|(s.r2?1:0);
      const auto axes=Tab5Emu::get().gamepad_axes();
      if(axes.rx<-10000)b|=2;
      if(axes.rx>10000)b|=1;
      if(axes.ry<-10000)b|=8;
      if(axes.ry>10000)b|=4;
      const auto usb=Tab5Emu::get().usb_gamepad_state();
      b|=(usb.up?0x800:0)|(usb.down?0x400:0)|(usb.left?0x200:0)|(usb.right?0x100:0);
      int x=(s.right?80:0)-(s.left?80:0),y=(s.up?80:0)-(s.down?80:0);
      if(abs(axes.lx)>4000 || abs(axes.ly)>4000){x=std::clamp(int(axes.lx)*80/32767,-80,80);y=std::clamp(-int(axes.ly)*80/32767,-80,80);}
      tab5_n64_input(machine_,b,x,y);
      if(!tab5_n64_run(machine_,12000)){
        tab5_n64_stats failure{};tab5_n64_get_stats(machine_,&failure);error_=failure.error;
        fmt::print("N64: {} PC={:08x}\n",error_,failure.pc);menu_->set_status(error_,true);Tab5Emu::get().request_menu();
      }
      tab5_n64_stats snapshot{};tab5_n64_get_stats(machine_,&snapshot);
      {std::lock_guard<std::mutex> lock(stats_mutex_);stats_=snapshot;}
      if(snapshot.vi_frames!=paced_vi_){
        const int64_t now=esp_timer_get_time();
        if(!next_vi_ || next_vi_<now-33334)next_vi_=now;
        next_vi_+=16667;paced_vi_=snapshot.vi_frames;
        if(next_vi_>now)vTaskDelay(pdMS_TO_TICKS((next_vi_-now+999)/1000));
      }
    }
    vTaskDelay(1);
#endif
    return Cart::run();
  }
  uint32_t game_frames() const override {
#if defined(ENABLE_N64)
    std::lock_guard<std::mutex> lock(stats_mutex_);return stats_.vi_frames;
#else
    return 0;
#endif
  }
  std::string core_metrics_json() const override{
#if defined(ENABLE_N64)
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return fmt::format("{{\"backend\":\"{}\",\"instructions\":{},\"rsp_instructions\":{},\"rdp_lists\":{},\"rom_reads\":{},\"exceptions\":{},\"pc\":{},\"vi_frames\":{},\"vi_width\":{},\"ai_buffers\":{},\"core_bytes\":{},\"hle_tasks\":{},\"hle_triangles\":{},\"hle_pixels\":{},\"run_us\":{},\"rsp_lle_us\":{},\"graphics_hle_us\":{},\"audio_hle_us\":{},\"audio_hle_tasks\":{},\"fast_cpu\":{},\"audio_hle\":{},\"raster_step\":{}}}",
#ifdef TAB5_N64_DAEDALUS
    "mupen-pure-daedalus-hle-software",
#else
    "mupen-pure-cxd4-software-rdp",
#endif
    stats_.instructions,stats_.rsp_instructions,stats_.rdp_lists,stats_.rom_reads,stats_.exceptions,stats_.pc,stats_.vi_frames,stats_.vi_width,stats_.ai_buffers,stats_.core_bytes,stats_.hle_tasks,stats_.hle_triangles,stats_.hle_pixels,stats_.run_us,stats_.rsp_lle_us,stats_.graphics_hle_us,stats_.audio_hle_us,stats_.audio_hle_tasks,options_.fast_cpu,options_.audio_hle,options_.raster_step);
#else
    return "{}";
#endif
  }
protected:
  bool persist_before_exit() override {
#if defined(ENABLE_N64)
    return !ready_ || tab5::content::primary_battery_store().flush(tab5_n64_battery(machine_));
#else
    return true;
#endif
  }
  void set_original_video_setting() override{display_original(320,240);}
  void set_fit_video_setting() override{display_fit(320,240);}
  void set_fill_video_setting() override{display_fill();}
  std::pair<size_t,size_t> get_video_size() const override{return {320,240};}
  std::span<uint8_t> get_video_buffer() const override{return {const_cast<uint8_t*>(frame_.data()),frame_.size()};}
  std::string get_save_extension() const override{return "_n64.sav";}
private:
  bool ready_=false;
  std::string error_;
  std::vector<uint8_t>frame_;
#if defined(ENABLE_N64)
  tab5_n64_options options_{};
  FILE*file_=nullptr;tab5_n64*machine_=nullptr;unsigned order_=0,rate_=0,frame_index_=0;tab5_n64_stats stats_{};
  mutable std::mutex stats_mutex_;
  uint64_t paced_vi_=0;int64_t next_vi_=0,last_poll_us_=0;
  static bool read_rom(void*opaque,uint32_t offset,void*out,size_t n){
    auto*self=static_cast<N64Cart*>(opaque);
    if(fseek(self->file_,offset,SEEK_SET)||fread(out,1,n,self->file_)!=n)return false;
    auto*p=static_cast<uint8_t*>(out);
    if(self->order_==1)for(size_t i=0;i<n;i+=2)std::swap(p[i],p[i+1]);
    if(self->order_==2)for(size_t i=0;i<n;i+=4){std::swap(p[i],p[i+3]);std::swap(p[i+1],p[i+2]);}
    return true;
  }
  static void video(void*opaque,const uint16_t*p,unsigned w,unsigned h){
    auto*self=static_cast<N64Cart*>(opaque);if(self->frame_.size()!=w*h*2)return;
    memcpy(self->frame_.data(),p,self->frame_.size());
    auto&emu=Tab5Emu::get();auto*dst=self->frame_index_?emu.frame_buffer1():emu.frame_buffer0();
    if(dst && emu.frame_slot_free() && emu.video_frame_available(dst)){
      memcpy(dst,p,self->frame_.size());emu.push_frame(dst);self->frame_index_^=1;
    }
  }
  static void audio(void*opaque,const int16_t*p,size_t n,unsigned rate){
    auto*self=static_cast<N64Cart*>(opaque);auto&emu=Tab5Emu::get();
    if(self->rate_!=rate){emu.audio_sample_rate(rate);self->rate_=rate;}
    emu.play_audio({reinterpret_cast<const uint8_t*>(p),n*4});
  }
  static void poll(void*context){auto*self=static_cast<N64Cart*>(context);const int64_t now=esp_timer_get_time();if(now-self->last_poll_us_>=4000){self->last_poll_us_=now;vTaskDelay(1);}}
#endif
};

#endif // TAB5_SM64_NATIVE
