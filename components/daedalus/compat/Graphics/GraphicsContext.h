#pragma once
#include "Graphics/ColourValue.h"
class CGraphicsContext {public:static CGraphicsContext*Get(){static CGraphicsContext c;return &c;}bool IsInitialised(){return true;}void BeginFrame(){}void EndFrame(){}void ViewportType(u32*w,u32*h){*w=320;*h=240;}void ClearAllSurfaces();void ClearZBuffer();void ClearColBuffer(c32);};
