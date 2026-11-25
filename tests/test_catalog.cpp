#include "rom_info.hpp"
#include "content_paths.hpp"
#include "library_index.hpp"
#include "catalog_request.hpp"
#include <array>
#include <cstring>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  {
    tab5::library::Query q;
    size_t offset = 99;
    assert(tab5::library::catalog_request(std::string("4928\0gba\0ACAO",13), q, offset));
    assert(offset == 4928 && q.page == 77 && q.system == "gba" && q.text == "ACAO");
    for (const auto &bad : {std::string("-1\0gba\0",7), std::string("x\0gba\0",6),
                           std::string("100001\0\0",8), std::string("0\0psx\0",6),
                           std::string("0\0gba\0x\0",8), std::string("0"),
                           std::string("0\0\0",3)+std::string(129,'x')}) {
      const auto before = offset;
      assert(!tab5::library::catalog_request(bad,q,offset));
      assert(offset == before);
    }
    assert(tab5::library::catalog_request(std::string("0\0n64\0",6),q,offset));
    assert(q.system=="n64");
  }
  namespace fs = std::filesystem;
  fs::path root(tab5::content::root);
  assert(fs::create_directory(root)); // Never remove a preexisting directory.
  fs::create_directories(root / "library");
  fs::create_directories(root / "roms/gba");
  fs::create_directories(root / "roms/gb/second");
  std::ofstream(root / "roms/gba/Ação.gba") << "not an executable ROM";
  std::ofstream(root / "roms/gb/second/same.gb") << "fixture";
  fs::create_directories(root / "roms/gba/.metadata");
  std::ofstream(root / "roms/gba/._Ação.gba") << "AppleDouble metadata";
  std::ofstream(root / "roms/gba/.metadata/ignored.gba") << "hidden folder";
  auto catalog = root / "library/games.json";
  std::ofstream(catalog) << R"({"schema_version":1,"games":[
    {"id":"good","system":"gba","title":"Ação, versão livre","rom":"roms/gba/Ação.gba","description":"Descrição em português","cover":"../../bad.jpg"},
    {"id":"bad","system":"gba","title":"escape","rom":"../escape.gba"},
    {"id":"good","system":"gb","title":"duplicate","rom":"roms/gb/second/same.gb"}]})";
  auto games = load_library();
  assert(games.size() == 2);
  assert(games[0].name == "Ação, versão livre");
  assert(games[0].description == "Descrição em português");
  assert(games[0].boxart_path.empty());
  // Invalid metadata degrades to directory discovery without dropping ROMs.
  for (auto invalid : {std::string("{bad json}"), std::string("{\"schema_version\":2,\"games\":[]}"),
                       std::string(1024 * 1024 + 1, ' '), std::string("\xff")}) {
    std::ofstream(catalog, std::ios::binary) << invalid;
    assert(load_library().size() == 2);
  }
  // A compact unknown array must not create an unbounded cJSON tree.
  std::string dense = "{\"schema_version\":1,\"games\":[],\"unknown\":[";
  for (unsigned i = 0; i < 40000; ++i) dense += "0,";
  dense += "0]}";
  std::ofstream(catalog) << dense;
  assert(load_library().size() == 2);
  // Disk-backed catalog: page 5000 entries without retaining them all.
  const std::string source=R"({"schema_version":1,"games":[]})";
  std::ofstream(catalog)<<source;fs::create_directories(root/"cache");
  std::vector<uint8_t> index(64,0);
  auto put32=[&](size_t offset,uint32_t value){for(unsigned n=0;n<4;++n)index[offset+n]=value>>(8*n);};
  for(unsigned n=0;n<5000;++n){
    std::array<std::string,6> fields={"gba","Ação "+std::to_string(n),"roms/gba/bench"+std::to_string(n)+".gba","","description","id"+std::to_string(n)};
    for(const auto &field:fields){index.push_back(field.size()&255);index.push_back(field.size()>>8);}
    for(const auto &field:fields)index.insert(index.end(),field.begin(),field.end());
  }
  memcpy(index.data(),"T5LIBI01",8);put32(8,1);put32(12,5000);put32(16,index.size());
  put32(20,tab5::library::crc32(std::span(index.data()+64,index.size()-64)));
  put32(24,source.size());put32(32,tab5::library::crc32(std::span((const uint8_t*)source.data(),source.size())));
  auto indexpath=root/"cache/library-v1.t5i";
  auto save_index=[&]{std::ofstream f(indexpath,std::ios::binary);f.write((const char*)index.data(),index.size());};
  save_index();
  tab5::library::Query query;query.page=77;
  auto page=tab5::library::read_index(indexpath.string(),query);
  assert(page.valid && page.total==5000 && page.games.size()==64 && page.games.front().name=="Ação 4928");
  query.page=78;page=tab5::library::read_index(indexpath.string(),query);
  assert(page.valid && page.games.size()==8);
  query.page=0;query.text="ACAO 4999";page=tab5::library::read_index(indexpath.string(),query);
  assert(page.valid&&page.total==1&&page.games.front().name=="Ação 4999");
  query.exact=true;query.text="roms/gba/bench4999.gba";
  page=tab5::library::read_index(indexpath.string(),query);
  assert(page.valid&&page.total==1&&page.games.front().name=="Ação 4999");
  query.text="ACAO 499";
  page=tab5::library::read_index(indexpath.string(),query);
  assert(page.valid&&page.total==1&&page.games.front().name=="Ação 499");
  query.exact=false;
  query.text="";query.view=1;query.favorites={tab5::content::resolve("roms/gba/bench4999.gba")};
  page=tab5::library::read_index(indexpath.string(),query);assert(page.valid&&page.total==1);
  assert(tab5::library::read_index(indexpath.string(),query,[]{return true;}).cancelled);
  // An SD-style read failure must be an explicit I/O error, never a C++
  // filebuf exception/abort or a successful empty/mismatched catalog.
  const auto catalogpath=root/"library/games.json";
  fs::rename(catalogpath,root/"library/games.saved");
  fs::create_directory(catalogpath);
  const auto unreadable=tab5::library::read_index(indexpath.string(),query);
  assert(!unreadable.valid && unreadable.io_error);
  fs::remove(catalogpath);fs::rename(root/"library/games.saved",catalogpath);
  index.back()^=1;save_index();assert(!tab5::library::read_index(indexpath.string(),query).valid);
  index.back()^=1;save_index();std::ofstream(catalog)<<"changed catalog";
  assert(!tab5::library::read_index(indexpath.string(),query).valid);
  fs::remove_all(root);
  std::cout << "catalog: actual firmware parser, UTF-8, duplicate/traversal, bounds and discovery OK\n";
}
