#pragma once
#include "HLEGraphics/TextureInfo.h"
#include <glm/glm.hpp>
class CNativeTexture {public:TextureInfo info;explicit CNativeTexture(const TextureInfo&i):info(i){}unsigned GetWidth(){return info.GetWidth();}unsigned GetHeight(){return info.GetHeight();}glm::vec2 GetScale(){return glm::vec2(1.f,1.f);}void InstallTexture(){}void*GetData(){return nullptr;}};
