// SPDX-License-Identifier: GPL-2.0-or-later
// Native software backend for the pinned Daedalus display-list/T&L pipeline.
// Uses the existing console CPU/devices; no OpenGL, dynarec or desktop API.
#include "Base/Types.h"
#include "Core/CPU.h"
#include "Core/Memory.h"
#include "Core/ROM.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/NativeTexture.h"
#include "HLEGraphics/BaseRenderer.h"
#include "HLEGraphics/DLParser.h"
#include "HLEGraphics/GraphicsPlugin.h"
#include "HLEGraphics/Microcode.h"
#include "HLEGraphics/RDPStateManager.h"
#include "Interface/Preferences.h"
#include "tab5_daedalus.h"
#include <array>
#include <limits>
#include <new>

u8 *g_pu8RamBase = nullptr, *g_pu8SpMemBase = nullptr;
Tab5CPUBridge gCPUState;
Tab5RomConfig g_ROM;
Tab5Preferences gGlobalPreferences;
bool gFogEnabled = false, gRumblePakActive = false, gCleanSceneEnabled = false,
     gClearDepthFrameBuffer = false;
u32 gCheckTextureHashFrequency = 0;
extern "C" u32 tab5_daedalus_ci_address();
extern "C" u32 tab5_daedalus_ci_width();
extern "C" u32 tab5_daedalus_ci_size();
static uint32_t *vi_regs, *mi_regs;
static void (*poll_fn)(void *);
static void *poll_context;
static uint64_t tasks, triangles, pixels;
static bool failed = false;
static unsigned poll_count = 0;
static unsigned raster_step = 1;
extern "C" void tab5_daedalus_render_step(unsigned step) {
  raster_step = step == 2 ? 2 : 1;
}
static unsigned command_budget = 0, pixel_budget = 0;
static constexpr unsigned RAM_BYTES = 8 * 1024 * 1024;
static std::array<float, 320 * 240> *depth;
static int scissor_x0 = 0, scissor_y0 = 0, scissor_x1 = 320, scissor_y1 = 240;
static void poll() {
  if (poll_fn)
    poll_fn(poll_context);
}
void tab5_daedalus_fail() { failed = true; }
bool tab5_daedalus_failed() { return failed; }
bool tab5_daedalus_command() {
  if (failed)
    return false;
  if (!command_budget) {
    failed = true;
    return false;
  }
  --command_budget;
  if ((++poll_count & 127) == 0)
    poll();
  return true;
}
void tab5_daedalus_scissor(int x, int y, int w, int h) {
  scissor_x0 = std::clamp(x, 0, 320);
  scissor_y0 = std::clamp(240 - y - h, 0, 240);
  scissor_x1 = std::clamp(x + w, 0, 320);
  scissor_y1 = std::clamp(240 - y, 0, 240);
}
static bool row(int y) {
  if ((y & 15) == 0)
    poll();
  if (failed)
    return false;
  if (pixel_budget < 320) {
    failed = true;
    return false;
  }
  pixel_budget -= 320;
  return true;
}
u32 Memory_VI_GetRegister(u32 reg) { return vi_regs[(reg & 0xffff) / 4 % 14]; }
void Memory_MI_SetRegisterBits(u32, u32 bits) { mi_regs[2] |= bits; }
struct RGBA {
  float r, g, b, a;
};
static RGBA color(c32 c) {
  return {float(c.GetR()), float(c.GetG()), float(c.GetB()), float(c.GetA())};
}
static uint8_t byte(uint32_t a) {
  if (a >= RAM_BYTES) {
    failed = true;
    return 0;
  }
  return g_pu8RamBase[a ^ 3];
}
static uint16_t half(uint32_t a) {
  return uint16_t(byte(a)) << 8 | byte(a + 1);
}
static RGBA rgba16(uint16_t v) {
  return {float((v >> 11) * 255 / 31), float(((v >> 6) & 31) * 255 / 31),
          float(((v >> 1) & 31) * 255 / 31), v & 1 ? 255.f : 0.f};
}
static uint32_t ci_width, ci_size, ci_address;
static void target() {
  ci_width = tab5_daedalus_ci_width();
  ci_size = tab5_daedalus_ci_size();
  ci_address = tab5_daedalus_ci_address();
}
static void put(int x, int y, RGBA c, float z, bool ztest, bool zwrite) {
  if (x < scissor_x0 || y < scissor_y0 || x >= scissor_x1 || y >= scissor_y1 ||
      x < 0 || y < 0 || x >= 320 || y >= 240)
    return;
  const auto width = ci_width, size = ci_size, address = ci_address;
  if (!width || width > 640 || (size != 2 && size != 3) || unsigned(x) >= width)
    return;
  if (ztest && z > (*depth)[y * 320 + x])
    return;
  if (c.a < 1)
    return;
  uint32_t a = address + (y * width + x) * (size == 2 ? 2 : 4);
  if (a + (size == 2 ? 2 : 4) > RAM_BYTES) {
    failed = true;
    return;
  }
  if (c.a < 254) {
    RGBA old = size == 2 ? rgba16(half(a))
                         : RGBA{float(byte(a)), float(byte(a + 1)),
                                float(byte(a + 2)), float(byte(a + 3))};
    const float alpha = c.a / 255;
    c.r = c.r * alpha + old.r * (1 - alpha);
    c.g = c.g * alpha + old.g * (1 - alpha);
    c.b = c.b * alpha + old.b * (1 - alpha);
  }
  auto channel = [](float v) { return unsigned(std::clamp(v, 0.f, 255.f)); };
  unsigned r = channel(c.r), g = channel(c.g), b = channel(c.b),
           al = channel(c.a);
  if (size == 2) {
    uint16_t v = (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1;
    g_pu8RamBase[a ^ 3] = v >> 8;
    g_pu8RamBase[(a + 1) ^ 3] = v;
  } else {
    g_pu8RamBase[a ^ 3] = r;
    g_pu8RamBase[(a + 1) ^ 3] = g;
    g_pu8RamBase[(a + 2) ^ 3] = b;
    g_pu8RamBase[(a + 3) ^ 3] = al;
  }
  if (zwrite)
    (*depth)[y * 320 + x] = z;
  ++pixels;
}
static int wrap(int n, int length, bool clamp, bool mirror) {
  if (length <= 0)
    return 0;
  if (clamp)
    return std::clamp(n, 0, length - 1);
  if (mirror) {
    int p = (n % (length * 2) + length * 2) % (length * 2);
    return p < length ? p : 2 * length - p - 1;
  }
  return (n % length + length) % length;
}
static float shift(float value, unsigned n) {
  return n <= 10 ? value / float(1u << n) : value * float(1u << (16 - n));
}
static RGBA sample(const TextureInfo &ti, const RDP_Tile &tile, float s,
                   float t) {
  if (!std::isfinite(s) || !std::isfinite(t) || fabsf(s) > 1e8f ||
      fabsf(t) > 1e8f) {
    failed = true;
    return {};
  }
  int x = wrap(int(floorf(s)), ti.GetWidth(), tile.clamp_s || !tile.mask_s,
               tile.mirror_s),
      y = wrap(int(floorf(t)), ti.GetHeight(), tile.clamp_t || !tile.mask_t,
               tile.mirror_t);
  if (!ti.GetWidth() || !ti.GetHeight())
    return {255, 255, 255, 255};
  uint64_t offset = uint64_t(ti.GetLoadAddress()) +
                    uint64_t(y) * ti.GetPitch() +
                    (uint64_t(x) << ti.GetSize()) / 2;
  if (offset >= RAM_BYTES) {
    failed = true;
    return {};
  }
  uint32_t a = uint32_t(offset);
  if (ti.IsSwapped() && (y & 1))
    a ^= ti.GetSize() == 3 ? 8 : 4;
  unsigned v = byte(a), fmt = ti.GetFormat(), sz = ti.GetSize();
  if (fmt == 0) {
    if (sz == 2)
      return rgba16(half(a));
    if (sz == 3)
      return {float(v), float(byte(a + 1)), float(byte(a + 2)),
              float(byte(a + 3))};
  }
  if (fmt == 2) {
    unsigned index =
        sz == 0 ? ((x & 1) ? v & 15 : v >> 4) + ti.GetPalette() * 16 : v;
    uint16_t p = half(ti.GetTlutAddress() + index * 2);
    if (ti.GetTLutFormat() == kTT_IA16)
      return {float(p >> 8), float(p >> 8), float(p >> 8), float(p & 255)};
    return rgba16(p);
  }
  if (fmt == 3) {
    if (sz == 0) {
      v = (x & 1) ? v & 15 : v >> 4;
      float c = (v >> 1) * 255 / 7.f;
      return {c, c, c, (v & 1) ? 255.f : 0.f};
    }
    if (sz == 1) {
      float c = (v >> 4) * 17.f;
      return {c, c, c, float((v & 15) * 17)};
    }
    if (sz == 2) {
      float c = byte(a);
      return {c, c, c, float(byte(a + 1))};
    }
  }
  if (fmt == 4) {
    float c = sz == 0 ? ((x & 1) ? v & 15 : v >> 4) * 17.f : float(v);
    return {c, c, c, c};
  }
  failed = true;
  return {};
}
static RGBA combine(uint64_t mux, unsigned cycle, RGBA combined, RGBA tex,
                    RGBA tex1, RGBA shade, RGBA prim, RGBA env, float lod) {
  const unsigned aa = cycle ? (mux >> 37) & 15 : (mux >> 52) & 15,
                 bb = cycle ? (mux >> 24) & 15 : (mux >> 28) & 15,
                 cc = cycle ? (mux >> 32) & 31 : (mux >> 47) & 31,
                 dd = cycle ? (mux >> 6) & 7 : (mux >> 15) & 7;
  const unsigned aaa = cycle ? (mux >> 21) & 7 : (mux >> 44) & 7,
                 bbb = cycle ? (mux >> 3) & 7 : (mux >> 12) & 7,
                 ccc = cycle ? (mux >> 18) & 7 : (mux >> 41) & 7,
                 ddd = cycle ? mux & 7 : (mux >> 9) & 7;
  const RGBA zero{0, 0, 0, 0}, one{255, 255, 255, 255};
  RGBA src[] = {combined, tex, tex1, prim, shade, env, one, zero};
  auto rgbsel = [&](unsigned n) { return n < 8 ? src[n] : zero; };
  auto alpha = [&](unsigned n) { return src[n & 7].a; };
  RGBA a = rgbsel(aa), b = rgbsel(bb), c = cc < 6 ? src[cc] : zero, d = src[dd];
  if (cc >= 7 && cc <= 12)
    c = {src[cc - 7].a, src[cc - 7].a, src[cc - 7].a, src[cc - 7].a};
  if (cc == 13 || cc == 14)
    c = {lod, lod, lod, lod};
  return {std::clamp((a.r - b.r) * c.r / 255 + d.r, 0.f, 255.f),
          std::clamp((a.g - b.g) * c.g / 255 + d.g, 0.f, 255.f),
          std::clamp((a.b - b.b) * c.b / 255 + d.b, 0.f, 255.f),
          std::clamp((alpha(aaa) - alpha(bbb)) *
                             (ccc == 0 || ccc == 6 ? lod : alpha(ccc)) / 255 +
                         alpha(ddd),
                     0.f, 255.f)};
}
class SoftwareRenderer final : public BaseRenderer {
public:
  void RestoreRenderStates() override {}
  RGBA texture(unsigned slot, float s, float t, bool texgen = false) {
    if (!mBoundTexture[slot])
      return {255, 255, 255, 255};
    const auto &info = mBoundTextureInfo[slot];
    const auto &tile = gRDPStateManager.GetTile(mActiveTile[slot] & 7);
    if (texgen) {
      s *= info.GetWidth();
      t *= info.GetHeight();
    } else {
      s = shift(s, tile.shift_s) - mTileTopLeft[slot].s / 4.f;
      t = shift(t, tile.shift_t) - mTileTopLeft[slot].t / 4.f;
    }
    return sample(info, tile, s, t);
  }

  void put_triangle(int x, int y, RGBA c, float z, bool test, bool write) {
    put(x, y, c, z, test, write);
  }
  void RenderTriangles(DaedalusVtx *v, u32 n, bool no_z) override {
    target();
    if (mTnL.Flags.Texture)
      UpdateTileSnapshots(mTextureTile);
    for (unsigned i = 0; i + 2 < n; i += 3) {
      struct V {
        float x, y, z, iw, s, t;
        RGBA c;
      };
      V p[3];
      bool ok = true;
      for (unsigned j = 0; j < 3; ++j) {
        auto q = mProjectionMat * glm::vec4(v[i + j].Position, 1.f);
        if (!std::isfinite(q.w) || q.w <= 0.00001f) {
          ok = false;
          break;
        }
        float iw = 1 / q.w;
        p[j] = {N64ToScreenX(mVpTrans.x + mVpScale.x * q.x * iw),
                N64ToScreenY(mVpTrans.y - mVpScale.y * q.y * iw),
                q.z * iw,
                iw,
                v[i + j].Texture.x,
                v[i + j].Texture.y,
                color(v[i + j].Colour)};
      }
      if (!ok)
        continue;
      auto edge = [](const V &a, const V &b, float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
      };
      float area = edge(p[0], p[1], p[2].x, p[2].y);
      if (!std::isfinite(area) || fabsf(area) < .001f)
        continue;
      int x0 = std::max(0, int(floorf(std::min({p[0].x, p[1].x, p[2].x})))),
          x1 = std::min(319, int(ceilf(std::max({p[0].x, p[1].x, p[2].x}))));
      int y0 = std::max(0, int(floorf(std::min({p[0].y, p[1].y, p[2].y})))),
          y1 = std::min(239, int(ceilf(std::max({p[0].y, p[1].y, p[2].y}))));
      if (raster_step == 2) {
        x0 = (x0 + 1) & ~1;
        y0 = (y0 + 1) & ~1;
      }
      for (int y = y0; y <= y1; y += raster_step) {
        if (!row(y))
          return;
        for (int x = x0; x <= x1; x += raster_step) {
          float a = edge(p[1], p[2], x + .5f, y + .5f) / area,
                b = edge(p[2], p[0], x + .5f, y + .5f) / area, c = 1 - a - b;
          if (a < 0 || b < 0 || c < 0)
            continue;
          RGBA shade{p[0].c.r * a + p[1].c.r * b + p[2].c.r * c,
                     p[0].c.g * a + p[1].c.g * b + p[2].c.g * c,
                     p[0].c.b * a + p[1].c.b * b + p[2].c.b * c,
                     p[0].c.a * a + p[1].c.a * b + p[2].c.a * c},
              tex{255, 255, 255, 255}, tex1 = tex;
          if (mTnL.Flags.Texture && mBoundTexture[0]) {
            float divisor = a * p[0].iw + b * p[1].iw + c * p[2].iw;
            float s = (p[0].s * a * p[0].iw + p[1].s * b * p[1].iw +
                       p[2].s * c * p[2].iw) /
                      divisor,
                  t = (p[0].t * a * p[0].iw + p[1].t * b * p[1].iw +
                       p[2].t * c * p[2].iw) /
                      divisor;
            tex = texture(0, s, t, mTnL.Flags.Light && mTnL.Flags.TexGen);
            if (gRDPOtherMode.cycle_type == CYCLE_2CYCLE)
              tex1 = texture(1, s, t, mTnL.Flags.Light && mTnL.Flags.TexGen);
          }
          RGBA out{0, 0, 0, 0};
          if (gRDPOtherMode.cycle_type == CYCLE_2CYCLE)
            out =
                combine(mMux, 0, out, tex, tex1, shade, color(mPrimitiveColour),
                        color(mEnvColour), mPrimLODFraction * 255);
          out = combine(mMux, 1, out, tex, tex1, shade, color(mPrimitiveColour),
                        color(mEnvColour), mPrimLODFraction * 255);
          put_triangle(x, y, out, p[0].z * a + p[1].z * b + p[2].z * c,
                       !no_z && mTnL.Flags.Zbuffer && gRDPOtherMode.z_cmp,
                       !no_z && gRDPOtherMode.z_upd);
        }
      }
      ++triangles;
    }
  }
  void FillRect(const glm::vec2 &a, const glm::vec2 &b, u32 value) override {
    target();
    RGBA c = color(c32(value));
    int x0 = std::clamp(int(a.x), 0, 320), y0 = std::clamp(int(a.y), 0, 240),
        x1 = std::clamp(int(b.x), 0, 320), y1 = std::clamp(int(b.y), 0, 240);
    for (int y = y0; y < y1; ++y) {
      if (!row(y))
        return;
      for (int x = x0; x < x1; ++x)
        put(x, y, c, 0, false, false);
    }
  }
  void TexRect(u32 tile, const glm::vec2 &a, const glm::vec2 &b, TexCoord s,
               TexCoord t) override {
    rectangle(tile, a, b, s, t, false);
  }
  void TexRectFlip(u32 tile, const glm::vec2 &a, const glm::vec2 &b, TexCoord s,
                   TexCoord t) override {
    rectangle(tile, a, b, s, t, true);
  }
  void rectangle(u32 idx, const glm::vec2 &a, const glm::vec2 &b, TexCoord s,
                 TexCoord t, bool flip) {
    target();
    UpdateTileSnapshots(idx);
    PrepareTexRectUVs(&s, &t);
    if (!mBoundTexture[0] || b.x <= a.x || b.y <= a.y)
      return;
    for (int y = std::max(0, int(a.y)); y < std::min(240, int(b.y)); ++y) {
      if (!row(y))
        return;
      for (int x = std::max(0, int(a.x)); x < std::min(320, int(b.x)); ++x) {
        float u = (x - a.x) / (b.x - a.x), v = (y - a.y) / (b.y - a.y);
        if (flip)
          std::swap(u, v);
        const float us = (s.s + (t.s - s.s) * u) / 32.f,
                    vt = (s.t + (t.t - s.t) * v) / 32.f;
        RGBA tex = texture(0, us, vt), tex1 = tex, out = tex;
        if (gRDPOtherMode.cycle_type == CYCLE_2CYCLE)
          tex1 = texture(1, us, vt);
        if (gRDPOtherMode.cycle_type != CYCLE_COPY) {
          RGBA shade{255, 255, 255, 255}, zero{0, 0, 0, 0};
          if (gRDPOtherMode.cycle_type == CYCLE_2CYCLE)
            zero = combine(mMux, 0, zero, tex, tex1, shade,
                           color(mPrimitiveColour), color(mEnvColour), 0);
          out = combine(mMux, 1, zero, tex, tex1, shade,
                        color(mPrimitiveColour), color(mEnvColour), 0);
        }
        put(x, y, out, mPrimDepth, false, false);
      }
    }
  }
  void Draw2DTexture(float x0, float y0, float x1, float y1, float u0, float v0,
                     float u1, float v1,
                     const std::shared_ptr<CNativeTexture> tex) override {
    target();
    if (!tex || x1 <= x0 || y1 <= y0)
      return;
    for (int y = std::max(0, int(y0)); y < std::min(240, int(y1)); ++y)
      for (int x = std::max(0, int(x0)); x < std::min(320, int(x1)); ++x) {
        RDP_Tile tile{};
        tile.clamp_s = tile.clamp_t = 1;
        put(x, y,
            sample(tex->info, tile, u0 + (u1 - u0) * (x - x0) / (x1 - x0),
                   v0 + (v1 - v0) * (y - y0) / (y1 - y0)),
            0, false, false);
      }
  }
  void Draw2DTextureR(float, float, float, float, float, float, float, float,
                      float, float,
                      const std::shared_ptr<CNativeTexture>) override {
    failed = true;
  }
};
BaseRenderer *gRenderer = nullptr;
CGraphicsPlugin::~CGraphicsPlugin() = default;
class Plugin final : public CGraphicsPlugin {
public:
  bool StartEmulation() override { return true; }
  void ViStatusChanged() override {}
  void ViWidthChanged() override {}
  void ProcessDList() override {}
  void UpdateScreen() override {}
  void RomClosed() override {}
};
std::unique_ptr<CGraphicsPlugin> gGraphicsPlugin;
void CGraphicsContext::ClearZBuffer() {
  depth->fill(std::numeric_limits<float>::infinity());
}
void CGraphicsContext::ClearColBuffer(c32 c) {
  auto *renderer = static_cast<SoftwareRenderer *>(gRenderer);
  if (renderer)
    renderer->FillRect({0, 0}, {320, 240}, c.GetColour());
}
void CGraphicsContext::ClearAllSurfaces() { ClearZBuffer(); }
void tab5_daedalus_framebuffer(const void *, unsigned, unsigned) {
  failed = true;
}
extern "C" bool tab5_daedalus_init(uint8_t *ram, uint8_t *sp, uint32_t *vi,
                                   uint32_t *mi, void (*fn)(void *),
                                   void *ctx) {
  g_pu8RamBase = ram;
  g_pu8SpMemBase = sp;
  vi_regs = vi;
  mi_regs = mi;
  poll_fn = fn;
  poll_context = ctx;
  tasks = triangles = pixels = 0;
  depth = new (std::nothrow) std::array<float, 320 * 240>;
  if (!depth)
    return false;
  gRenderer = new (std::nothrow) SoftwareRenderer;
  if (!gRenderer) {
    delete depth;
    depth = nullptr;
    return false;
  }
  gGraphicsPlugin.reset(new (std::nothrow) Plugin);
  if (!gGraphicsPlugin) {
    tab5_daedalus_close();
    return false;
  }
  g_ROM = {};
  g_ROM.settings.GameName = "Tab5 cartridge";
  GBIMicrocode_Reset();
  if (!DLParser_Initialise()) {
    tab5_daedalus_close();
    return false;
  }
  return true;
}
extern "C" void tab5_daedalus_close() {
  if (gRenderer)
    DLParser_Finalise();
  delete gRenderer;
  gRenderer = nullptr;
  gGraphicsPlugin.reset();
  delete depth;
  depth = nullptr;
  g_pu8RamBase = g_pu8SpMemBase = nullptr;
}
static void resolve_low_resolution() {
  if (raster_step != 2 || !ci_width || (ci_width & 1) || ci_width > 640 ||
      ci_address >= RAM_BYTES)
    return;
  unsigned bytes = ci_size == 2 ? 2 : ci_size == 3 ? 4 : 0;
  if (!bytes || ci_address + (uint64_t)240 * ci_width * bytes > RAM_BYTES ||
      (ci_address & 3))
    return;
  auto *base = reinterpret_cast<uint32_t *>(g_pu8RamBase + ci_address);
  for (unsigned y = 0; y < 240; y += 2) {
    auto *row = base + y * ci_width * bytes / 4;
    auto *next = row + ci_width * bytes / 4;
    if (bytes == 2)
      for (unsigned x = 0; x < std::min<uint32_t>(320, ci_width) / 2; ++x) {
        uint32_t v = (row[x] >> 16) * 0x10001u;
        row[x] = next[x] = v;
      }
    else
      for (unsigned x = 0; x < std::min<uint32_t>(320, ci_width); x += 2) {
        uint32_t v = row[x];
        row[x] = row[x + 1] = next[x] = next[x + 1] = v;
      }
  }
}
extern "C" int tab5_daedalus_task() {
  if (!gRenderer)
    return -1;
  auto *t = reinterpret_cast<uint32_t *>(g_pu8SpMemBase + 0xfc0);
  if (t[0] != 1)
    return 0;
  for (unsigned n : {4u, 6u, 12u})
    if ((t[n] & 0x1fffffff) >= RAM_BYTES)
      return -1;
  if ((t[4] & 0x1fffffff) > RAM_BYTES - 4096 ||
      t[7] > RAM_BYTES - (t[6] & 0x1fffffff))
    return -1;
  failed = false;
  poll_count = 0;
  command_budget = 40000;
  pixel_budget = 8000000;
  scissor_x0 = scissor_y0 = 0;
  scissor_x1 = 320;
  scissor_y1 = 240;
  DLParser_Process(40000);
  if (!failed)
    resolve_low_resolution();
  ++tasks;
  return failed ? -1 : 1;
}
extern "C" void tab5_daedalus_metrics(uint64_t *a, uint64_t *b, uint64_t *c) {
  *a = tasks;
  *b = triangles;
  *c = pixels;
}
