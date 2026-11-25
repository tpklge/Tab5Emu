#pragma once
#include "Graphics/NativeTexture.h"
class CTextureCache {public:static CTextureCache*Get(){static CTextureCache c;return &c;}std::shared_ptr<CNativeTexture>GetOrCreateTexture(const TextureInfo&i){return std::make_shared<CNativeTexture>(i);}void PurgeOldTextures(){}void DropTextures(){}};
