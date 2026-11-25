#include "content_paths.hpp"
#include "battery_level.hpp"
#include "content_storage.hpp"
#include "keyboard_mapping.hpp"
#include "touch_controls.hpp"
#include "frame_metrics.hpp"
#include "gb_state_layout.hpp"
#include "usb_msc_gate.hpp"
#include "pause_shortcut.hpp"
#include "video_leases.hpp"
#include "gb_audio_filter.h"
#include "gb_rom.hpp"
#include "gamepad_merge.hpp"
#include "key_edges.hpp"
#include "render_budget.hpp"
#include "../components/gba/rv32/smc_tags.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
#include <thread>
#include <future>
#include <vector>

int main() {
  for(auto profile:{tab5::input::Profile::Snes,tab5::input::Profile::Gba,tab5::input::Profile::N64}){
    const auto buttons=tab5::touch::buttons_for(profile);
    for(size_t i=0;i<buttons.size();++i)for(size_t j=i+1;j<buttons.size();++j){
      const auto&a=buttons[i];const auto&b=buttons[j];
      assert(a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y);
    }
  }

  assert(tab5::power::estimated_percent(true,8.235f)==91);
  assert(tab5::power::estimated_percent(true,8.4f)==100);
  assert(tab5::power::estimated_percent(true,6.5f)==0);
  assert(tab5::power::estimated_percent(false,8.0f)==-1);
  assert(tab5::power::estimated_percent(true,4.2f)==-1);
  {
    uint8_t keys[32]{};keys[17>>3]|=1u<<(17&7);keys[16>>3]|=1u<<(16&7);
    const auto n64=tab5::input::map(keys,tab5::input::Profile::N64);
    assert(n64.l2 && n64.r2);
    GamepadState touched{};bool menu=false;
    assert(tab5::touch::hit(1180,390,tab5::input::Profile::N64,touched,menu));
    assert(touched.l2 && !menu);
  }
  {
    tab5::input::KeyEdges edges;
    uint8_t keys[32]{};
    keys[5] = 8;
    assert(edges.update(keys)[5] == 8);
    assert(edges.update(keys)[5] == 0);
    keys[5] = 0; edges.update(keys);
    keys[5] = 8; assert(edges.update(keys)[5] == 8);
    keys[31] = 128; assert(edges.update(keys)[31] == 128);
  }
  {
    std::vector<uint8_t> rom(32768);
    assert(!tab5::gb::rom_error(rom)); // Homebrew without a proprietary logo.
    for (size_t n = 0; n < 0x150; ++n)
      assert(tab5::gb::rom_error({rom.data(), n}));
    rom[0x147] = 0x01; rom[0x148] = 1;
    assert(tab5::gb::rom_error(rom)); // Declared second bank pair is missing.
    rom.resize(65536);
    assert(!tab5::gb::rom_error(rom));
    for (uint8_t mapper : {0x00,0x01,0x02,0x03,0x05,0x06,0x08,0x09,
                           0x0f,0x10,0x11,0x12,0x13,0x19,0x1a,0x1b,
                           0x1c,0x1d,0x1e,0xfe,0xff}) {
      rom[0x147] = mapper;
      assert(!tab5::gb::rom_error(rom));
    }
    for (uint8_t mapper : {0x0b, 0x20, 0x22, 0xfc}) {
      rom[0x147] = mapper;
      assert(tab5::gb::rom_error(rom));
    }
    rom[0x147] = 0;
    for (uint8_t size : {9, 0x52, 0x53, 0x54, 0xff}) {
      rom[0x148] = size;
      assert(tab5::gb::rom_error(rom)); // No unsupported rounded-up geometry.
    }
    rom[0x148] = 0; rom[0x149] = 6;
    assert(tab5::gb::rom_error(rom));
    rom[0x149] = 5;
    assert(!tab5::gb::rom_error(rom));
  }
  {
    using namespace tab5::input;
    GamepadInput first, second, merged;
    first.buttons.buttons = 1; first.menu = true; first.axes.lx = -32768;
    second.buttons.buttons = 2; second.axes.lx = 12000; second.axes.ry = 22000;
    merge_gamepad(merged, first); merge_gamepad(merged, second);
    assert(merged.buttons.a && merged.buttons.b && merged.menu);
    assert(merged.axes.lx == -32768 && merged.axes.ry == 22000);
    // A neutral report from pad 2 retains pad 1; unplug pad 1 retains pad 2.
    merged = {}; merge_gamepad(merged, first); merge_gamepad(merged, {});
    assert(merged.buttons.a && !merged.buttons.b && merged.menu);
    merged = {}; merge_gamepad(merged, second);
    assert(!merged.buttons.a && merged.buttons.b && !merged.menu);
    assert(merged.axes.lx == 12000 && merged.axes.ry == 22000);
    merged = {}; assert(!merged.buttons.buttons && !merged.axes.lx && !merged.menu);
  }
  {
    tab5::video::RenderBudget budget;
    assert(budget.should_render(true,16667));
    budget.completed(true,8000);assert(budget.should_render(true,16667));
    budget.completed(true,25000);assert(!budget.should_render(true,16667));
    for(unsigned n=0;n<3;++n){assert(!budget.should_render(true,16667));budget.completed(false,12000);}
    assert(budget.should_render(true,16667)); // never starve presentation
    budget.completed(true,25000);budget.completed(false,22000);
    assert(budget.should_render(true,16667)); // skipping cannot cure CPU overload
    assert(budget.should_render(false,16667));
    budget.reset();assert(budget.should_render(true,16667));
  }
  {
    tab5::input::PauseShortcut chord;
    assert(!chord.update(true,true,100));
    assert(!chord.update(true,true,450099));
    assert(chord.update(true,true,450100));
    assert(!chord.update(true,true,1000000));
    assert(!chord.update(false,true,1100000));
    assert(!chord.update(true,true,1700000));
    assert(!chord.update(false,false,1800000));
    assert(!chord.update(true,true,1900000));
    assert(chord.update(true,true,2350000));
    tab5::video::Leases leases;int a=0,b=0,c=0;
    assert(leases.retain(&a) && leases.retain(&b));
    assert(!leases.available(&a)); // consumer rendering A
    leases.release(&b);assert(leases.retain(&c)); // queued B replaced by C
    assert(leases.available(&b) && !leases.available(&c) && !leases.available(&a));
    assert(leases.retain(&a));leases.release(&a);assert(!leases.available(&a));
    leases.release(&a);leases.release(&c);assert(leases.idle());
    Tab5GbAudioFilter f{};
    assert(tab5_gb_master_gain(0x70,0)==8 && tab5_gb_master_gain(0x70,1)==1);
    assert(tab5_gb_master_gain(0x07,0)==1 && tab5_gb_master_gain(0x07,1)==8);
    assert(tab5_gb_master_gain(0x00,0)==1 && tab5_gb_master_gain(0x00,1)==1);
    assert(tab5_gb_high_pass(&f,0,16000,1,0)==16000);
    for(int n=0;n<32768;++n)tab5_gb_high_pass(&f,0,16000,1,0);
    assert(std::abs(tab5_gb_high_pass(&f,0,16000,1,0))<2);
    assert(tab5_gb_high_pass(&f,1,0,1,0)==0); // stereo isolated
    auto held=f.capacitor[0];assert(tab5_gb_high_pass(&f,0,12000,0,0)==0 && f.capacitor[0]==held);
    assert(tab5_gb_high_pass(&f,0,0,1,0)<-15000); // DC step is AC-coupled
  }
  {
    using namespace tab5::usb;
    MscIoGate gate;int calls=0;
    auto backend=[&]{++calls;return 512;};
    assert(gate.run(HostIo::Read,backend)==-1);
    assert(gate.run(HostIo::Write,backend)==-1);
    assert(gate.run(HostIo::Ready,backend)==-1 && calls==0);
    gate.allow(true);assert(gate.run(HostIo::Read,backend)==512);
    std::promise<void> accepted,release,closing;
    auto released=release.get_future().share();
    std::thread transfer([&]{assert(gate.run(HostIo::Write,[&]{
      accepted.set_value();released.wait();return backend();
    })==512);});
    accepted.get_future().wait();
    auto close=std::async(std::launch::async,[&]{closing.set_value();gate.allow(false);});
    closing.get_future().wait();
    assert(close.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout);
    release.set_value();transfer.join();close.get();
    assert(gate.run(HostIo::Read,backend)==-1 && calls==2);
    const auto stats=gate.snapshot();
    assert(!stats.allowed && stats.blocked_reads==2 && stats.blocked_writes==1 && stats.blocked_ready==1);
  }
  {
    std::vector<uint8_t> ew(0x40000), iw(0x8000);
    assert(!tab5_rv32_ram_has_code(0x03000100,4,ew.data(),iw.data()));
    iw[0x103]=0x80; // tag in the high byte of the second halfword
    assert(tab5_rv32_ram_has_code(0x03000100,4,ew.data(),iw.data()));
    assert(tab5_rv32_ram_has_code(0x03008103,1,ew.data(),iw.data()));
    assert(tab5_rv32_ram_has_code(0x03000103,2,ew.data(),iw.data()));
    assert(!tab5_rv32_ram_has_code(0x03000100,2,ew.data(),iw.data()));
    ew[0x3ffff]=1;
    assert(tab5_rv32_ram_has_code(0x0207ffff,1,ew.data(),iw.data()));
    assert(tab5_rv32_ram_has_code(0x0203ffff,4,ew.data(),iw.data()));
    assert(!tab5_rv32_ram_has_code(0x0803ffff,4,ew.data(),iw.data()));
  }
  {
    using tab5::input::Profile;
    GamepadState pad{};bool menu=false;
    assert(tab5::touch::hit(52,318,Profile::GameBoy,pad,menu));
    assert(pad.up && pad.left && !pad.right && !pad.down);
    pad={};assert(!tab5::touch::hit(640,360,Profile::Snes,pad,menu));
    for (const auto profile:{Profile::GameBoy,Profile::Gba,Profile::Snes,Profile::MegaDrive}) {
      for (const auto &button:tab5::touch::buttons) {
        assert(button.x>=0 && button.x+button.w<=1280);
        assert(button.x+button.w<=208 || button.x>=1072);
        pad={};menu=false;
        const bool hit=tab5::touch::hit(button.x+button.w/2,button.y+button.h/2,profile,pad,menu);
        assert(hit==tab5::touch::active(button,profile));
        if(hit) {assert(pad.buttons==button.mask);assert(menu==(button.mask==0));}
      }
    }
    pad={};menu=false;assert(!tab5::touch::hit(-1,500,Profile::Gba,pad,menu));
    assert(!tab5::touch::hit(1280,720,Profile::Gba,pad,menu));
  }

  using namespace tab5;
  for (auto path : {"../x", "/x", "x//y", "x/../y", "x/./y", "x/", "a\\b", "C:/x"})
    assert(!content::valid_relative(path));
  assert(content::valid_relative("roms/gba/Ação livre.gba"));
  assert(content::resolve("roms/gb/a.gb") == "/sdcard/Tab5-Emu/roms/gb/a.gb");
  input::Matrix keys;
  uint8_t bitmap[32]{};
  // E+A+I+O+J+K+T+Y held simultaneously, actual A164 row/columns.
  for (uint8_t raw : {0xa3, 0xb2, 0xa8, 0xa9, 0xb8, 0xb9, 0xa5, 0xa6}) assert(keys.event(raw));
  keys.bitmap(bitmap);
  auto snes = input::map(bitmap, input::Profile::Snes);
  assert(snes.up && snes.left && snes.x && snes.a && snes.y && snes.b && snes.start && snes.select);
  auto gb = input::map(bitmap, input::Profile::GameBoy);
  assert(gb.up && gb.a && gb.b && !gb.x && !gb.y);
  auto md = input::map(bitmap, input::Profile::MegaDrive);
  assert(md.b && md.a && md.y && !md.select);
  keys.event(0xb3); // S cancels E
  keys.bitmap(bitmap);
  auto opposite = input::map(bitmap, input::Profile::Snes);
  assert(!opposite.up && !opposite.down && opposite.left);
  keys.held.fill(false);
  keys.event(0xa2); // physical W is deliberately unmapped
  keys.bitmap(bitmap);
  assert(input::map(bitmap, input::Profile::Snes).buttons == 0);
  keys.held.fill(false);
  keys.bitmap(bitmap);
  assert(input::map(bitmap, input::Profile::Snes).buttons == 0);
  assert(!keys.event(0xff) && !keys.event(0xee));
  // Two physical positions share HID '1'; releasing one retains the other.
  keys.event(0x81); keys.event(0x91); keys.event(0x01); keys.bitmap(bitmap);
  assert(bitmap[30 >> 3] & (1 << (30 & 7)));

  auto root = std::filesystem::temp_directory_path() / ("tab5-emu-host-" + std::to_string(getpid()));
  std::filesystem::create_directory(root);
  auto path = (root / "save.bin").string();
  assert(content::atomic_write(path, "first", 5));
  assert(content::atomic_write(path, "second", 6));
  auto read = [](const std::string &name) {
    std::ifstream f(name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
  };
  assert(read(path) == "second" && read(path + ".bak") == "first");
  // Simulate interruption after the previous generation moved to .bak.
  std::filesystem::remove(path);
  assert(read(content::readable_generation(path)) == "first");
  assert(content::atomic_write(path, "third", 5));
  assert(read(path) == "third");
  std::filesystem::remove_all(root);
  FrameMetrics metrics;
  assert(metrics.snapshot().frames == 0 && metrics.snapshot().p99_us == 0);
  for (unsigned i = 1; i <= 1100; ++i)
    metrics.record({.run_us=i, .video_us=2, .audio_us=3, .pacing_us=4,
                    .audio_requested_bytes=100, .audio_accepted_bytes=80, .drawn=bool(i % 2)});
  const auto sampled = metrics.snapshot();
  assert(sampled.frames == 1100 && sampled.drawn == 550 && sampled.run_us == 605550);
  assert(sampled.window_samples == 1024 && sampled.median_us == 588 && sampled.p95_us == 1049 && sampled.p99_us == 1090);
  assert(sampled.audio_requested_bytes == 110000 && sampled.audio_accepted_bytes == 88000);
  metrics.reset();
  assert(metrics.snapshot().frames == 0 && metrics.snapshot().window_samples == 0);
  std::thread writer([&] { for (unsigned i=0;i<3000;++i) metrics.record({.run_us=i+1}); });
  auto reader=[&] {
    for (unsigned i=0;i<100;++i) {
      auto sample=metrics.snapshot();
      assert(sample.frames>=sample.window_samples);
      assert(sample.median_us<=sample.p95_us && sample.p95_us<=sample.p99_us);
    }
  };
  std::thread first_reader(reader), second_reader(reader);
  writer.join(); first_reader.join(); second_reader.join();
  assert(metrics.frames()==3000);
  // Validate the same gnuboy header bounds used before its legacy loader.
  std::array<uint8_t,4096> header{};
  const char *tags[]={"GbSs","cgb ","iram","vram","sram","hi  ","pal ","oam ","wav "};
  const uint32_t values[]={0x105,1,1,9,13,3328,3584,3840,3312};
  for (unsigned i=0;i<9;++i) {
    memcpy(header.data()+8*i,tags[i],4);
    for (unsigned byte=0;byte<4;++byte) header[8*i+4+byte]=values[i]>>(byte*8);
  }
  assert(tab5::gb::state_bytes(true,4)==86016);
  assert(tab5::gb::valid_state_header(header.data(),header.size(),true,4));
  assert(!tab5::gb::valid_state_header(header.data(),header.size(),false,4));
  assert(!tab5::gb::valid_state_header(header.data(),4095,true,4));
  header[5*8+4]=0xff;assert(!tab5::gb::valid_state_header(header.data(),header.size(),true,4));
  std::cout << "platform: paths, A164 multi-key/profiles/disconnect, save recovery OK\n";
}
