#include "input_profiles.hpp"
#include "content_paths.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main() {
  using namespace tab5::input;
  constexpr Profile profiles[]={Profile::Snes,Profile::GameBoy,Profile::Gba,Profile::MegaDrive};
  std::ifstream packaged("sdcard/Tab5-Emu/config/input_profiles.json");
  assert(packaged);
  const std::string supplied(std::istreambuf_iterator<char>(packaged),{});
  const char *systems[]={"snes","gb","gba","megadrive"};
  for (unsigned i=0;i<4;++i) {
    const auto selected=parse_profile(supplied,profiles[i],systems[i]);
    assert(selected.valid && selected.bindings.keys==defaults(profiles[i]).keys);
  }
  uint8_t keys[32]{};
  // Every HID identity retains the established defaults, including arrows.
  for (auto p:profiles) for (unsigned u=0;u<256;++u) {
    for (auto &k:keys) k=0;
    keys[u>>3]|=1u<<(u&7);
    assert(map(keys,p).buttons==map(keys,defaults(p)).buttons);
  }
  const std::string hash(64,'a');
  const std::string text=R"({"schema_version":1,"global":{"l":"Q","r":"R"},
    "systems":{"gba":{"a":"F"}},"games":{")"+hash+R"(":{"a":"G","b":"H"}}})";
  const auto game=parse_profile(text,Profile::Gba,"gba",hash);
  assert(game.valid && game.from_file && game.bindings.keys[0]==10 && game.bindings.keys[1]==11);
  assert(game.bindings.keys[10]==20 && game.bindings.keys[11]==21);
  const auto console=parse_profile(text,Profile::Gba,"gba");
  assert(console.valid && console.bindings.keys[0]==9 && console.bindings.keys[1]==14);
  const auto legacy=parse_profile(R"({"schema_version":1,"gb":{"a":"F"},"n64_proposta_nao_executavel":{"analog":["E","A","S","D"]}})",Profile::GameBoy,"gbc");
  assert(legacy.valid && legacy.bindings.keys[0]==9);
  const auto md=parse_profile(R"({"schema_version":1,"systems":{"megadrive":{"a":"F","b":"G","c":"H"}}})",Profile::MegaDrive,"megadrive");
  assert(md.valid && md.bindings.keys[1]==9 && md.bindings.keys[0]==10 && md.bindings.keys[3]==11);
  const auto alias=parse_profile(R"({"schema_version":1,"systems":{"megadrive":{"a":"F","b":"G","c":"H"}}})",Profile::MegaDrive,"genesis");
  assert(alias.valid && alias.bindings.keys == md.bindings.keys);
  const auto alias_edit=edit_profile(R"({"schema_version":1})",Profile::MegaDrive,"genesis","",Scope::System,md.bindings,(1u<<0)|(1u<<1)|(1u<<3),false);
  assert(alias_edit.valid && alias_edit.text.find("megadrive")!=std::string::npos && alias_edit.text.find("genesis")==std::string::npos);
  for (auto invalid:{
    R"({"schema_version":1,"global":{"up":"A"}})", // conflict with left
    R"({"schema_version":1,"global":{"a":"ESC"}})", // recovery is reserved
    R"({"schema_version":1,"global":{"a":"UP"}})", // implicit arrow alias conflict
    R"({"schema_version":1,"systems":{"gba":{"x":"F"}}})", // no invented GBA button
    R"({"schema_version":1,"global":{"a":"F","a":"G"}})",
    R"({"schema_version":1,"global":{"a":"F","\u0061":"G"}})",
    R"({"schema_version":1,"global":{"a":"\u0000"}})",
    R"({"schema_version":1,"global":{"a":"INVALID"}})",
    R"({"schema_version":1,"global":{"unknown":"F"}})",
    R"({"schema_version":1,"systems":[]})",R"({"schema_version":2})","{bad json}"}) {
    const auto fallback=parse_profile(invalid,Profile::Gba,"gba",hash);
    assert(!fallback.valid && fallback.from_file && !fallback.error.empty());
    assert(fallback.bindings.keys==defaults(Profile::Gba).keys);
  }
  assert(!parse_profile(std::string(65537,' '),Profile::Gba,"gba").valid);
  assert(!parse_profile("[[[[[[[0]]]]]]]",Profile::Gba,"gba").valid);
  auto disabled=parse_profile(R"({"schema_version":1,"systems":{"gba":{"a":"NONE","menu":"F1"}}})",Profile::Gba,"gba");
  assert(disabled.valid && disabled.bindings.keys[0]==0);
  for (auto &k:keys) k=0;
  keys[58>>3]|=1u<<(58&7);
  assert(menu_pressed(keys,disabled.bindings));
  keys[58>>3]=0; keys[41>>3]|=1u<<(41&7);
  assert(menu_pressed(keys,disabled.bindings));
  // A held Start must release before it can act in the new focus/profile.
  KeyGate gate; keys[23>>3]|=1u<<(23&7); gate.block(keys);
  uint8_t filtered[32]; std::copy_n(keys,32,filtered); gate.filter(filtered);
  assert(!map(filtered,defaults(Profile::Gba)).start);
  keys[23>>3]&=~(1u<<(23&7)); gate.release(keys);
  keys[23>>3]|=1u<<(23&7); std::copy_n(keys,32,filtered); gate.filter(filtered);
  assert(map(filtered,defaults(Profile::Gba)).start);
  namespace fs=std::filesystem;
  fs::path root(tab5::content::root);
  assert(fs::create_directory(root));
  assert(!load_profile(Profile::Gba,"gba").from_file);
  fs::create_directory(root/"config");
  std::ofstream(root/"config/input_profiles.json")<<text;
  assert(load_profile(Profile::Gba,"gba",hash).bindings.keys==game.bindings.keys);
  auto six=parse_profile(supplied,Profile::MegaDrive6,"megadrive");assert(six.valid);
  assert(six.bindings.keys[2]==12&&six.bindings.keys[10]==24&&six.bindings.keys[11]==19);
  auto chosen=defaults(Profile::Gba);chosen.keys[0]=9;
  auto edit=edit_profile(supplied,Profile::Gba,"gba",hash,Scope::Game,chosen,1);
  assert(edit.valid&&parse_profile(edit.text,Profile::Gba,"gba",hash).bindings.keys[0]==9);
  auto reset=edit_profile(edit.text,Profile::Gba,"gba",hash,Scope::Game,{},0,true);
  assert(reset.valid&&reset.bindings.keys[0]==18);
  chosen.keys[0]=4;assert(!edit_profile(supplied,Profile::Gba,"gba",hash,Scope::Game,chosen,1).valid);
  assert(!edit_profile(supplied,Profile::Gba,"gba","",Scope::Game,chosen,1).valid);
  chosen.keys[0]=9;
  auto old=R"({"schema_version":1,"future":{"preserve":true},"gb":{"a":"G"},"gba":{"b":"H"}})";
  auto migrated=edit_profile(old,Profile::Gba,"gba",hash,Scope::System,chosen,1);
  assert(migrated.valid&&migrated.text.find("preserve")!=std::string::npos);
  assert(parse_profile(migrated.text,Profile::GameBoy,"gb").bindings.keys[0]==10);
  assert(parse_profile(migrated.text,Profile::Gba,"gba").bindings.keys[1]==11);
  assert(!edit_profile("{invalid}",Profile::Gba,"gba",hash,Scope::System,chosen,1).valid);
  fs::remove_all(root);
  std::cout<<"input profiles: precedence, defaults, legacy, conflicts, bounds and focus gate OK\n";
}
