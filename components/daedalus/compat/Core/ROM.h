#pragma once
#include <string>
enum Tab5GameHack {NO_GAME_HACK,BANJO_TOOIE,CHAMELEON_TWIST_2,DKR,GEX_GECKO,SUPER_BOWLING,TARZAN,TIGERS_HONEY_HUNT,WCW_NITRO,WONDER_PROJECTJ2,WORMS_ARMAGEDDON,YOSHI,ZELDA_MM};
struct Tab5RomConfig {unsigned TvType=1;Tab5GameHack GameHacks=NO_GAME_HACK;bool LOAD_T1_HACK=false,T1_HACK=false,ZELDA_HACK=false;unsigned TLUT_HACK=0;struct {std::string GameName;} settings;};extern Tab5RomConfig g_ROM;
