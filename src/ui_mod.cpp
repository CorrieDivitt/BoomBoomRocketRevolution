// Front-end modding: lets the port replace or add the game's Lua UI screens, add text,
// and gives those screens a way to talk to the PC side. Used by the Revolution settings
// screen (ui_scripts/Options/BBRR.lua).
//
// The game's UI is Lua 5.1. Its scripts are packed (zlib, precompiled) inside the
// executable and loaded by name; the Lua build still contains the compiler, so plain
// source text loads too.
//
//   sub_820A6898(L, name)            the game's "load script by name" (luaL_loadfile-like)
//   sub_820A6708(L, buf, size, name) luaL_loadbuffer
//   sub_8209BC38(loc, L)             loads the language .ini and fills the Lua table UIText
//   sub_8209B8D0(loc, L, file)       adds "KEY = text" lines from an (in-memory) file;
//                                    expects UIText on top of the Lua stack
//   sub_820A79C8(L)                  Lua gcinfo() - unused by the game, our bridge
//   Lua API: lua_type 820A45B0, lua_tolstring 820A4900, lua_pushstring 820A4CF8,
//            lua_getfield 820A5010, lua_settop 820A4348; malloc 8215EC78, free 8215FEA8
//
// Script overrides are read from a "ui_scripts" folder next to the exe (or in a parent
// folder) every time the game loads that script, so edits show up without a rebuild.

#include "settings.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

REXCVAR_DEFINE_BOOL(ui_mods, true, "BBR", "Load replacement UI scripts (ui_scripts folder) and the Revolution settings screen");

REX_EXTERN(__imp__sub_820A6898);
REX_EXTERN(__imp__sub_8209BC38);
REX_EXTERN(__imp__sub_820A79C8);
REX_EXTERN(sub_820A6708);
REX_EXTERN(sub_8209B8D0);
REX_EXTERN(sub_820A45B0);
REX_EXTERN(sub_820A4900);
REX_EXTERN(sub_820A4CF8);
REX_EXTERN(sub_820A5010);
REX_EXTERN(sub_820A4348);
REX_EXTERN(sub_8215EC78);
REX_EXTERN(sub_8215FEA8);

namespace {
namespace fs = std::filesystem;
using GuestFn = void (*)(PPCContext&, uint8_t*);

constexpr uint32_t kUITextName = 0x82004CBC;  // "UIText" in the game's read-only data
constexpr int64_t kLuaGlobalsIndex = -10002;
constexpr uint32_t kLuaTString = 4;

uint8_t* Host(uint8_t* base, uint32_t addr) {
#if defined(_WIN32)
  return base + addr + (addr >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + addr;
#endif
}
void Store32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, addr), &v, 4);
}
std::string GuestString(uint8_t* base, uint32_t addr) {
  if (!addr) return {};
  const char* p = reinterpret_cast<const char*>(Host(base, addr));
  return std::string(p, strnlen(p, 1024));
}

// Calls a recompiled game function with up to four integer arguments.
uint32_t Call(GuestFn fn, PPCContext& ctx, uint8_t* base, int64_t a = 0, int64_t b = 0, int64_t c = 0, int64_t d = 0) {
  ctx.r3.s64 = a;
  ctx.r4.s64 = b;
  ctx.r5.s64 = c;
  ctx.r6.s64 = d;
  fn(ctx, base);
  return ctx.r3.u32;
}
uint32_t GuestAlloc(PPCContext& ctx, uint8_t* base, uint32_t size) { return Call(sub_8215EC78, ctx, base, size); }
void GuestFree(PPCContext& ctx, uint8_t* base, uint32_t ptr) { if (ptr) Call(sub_8215FEA8, ctx, base, ptr); }

fs::path ScriptsFolder() {
  static fs::path cached = [] {
    std::error_code ec;
    for (fs::path dir = rex::filesystem::GetExecutableFolder(); !dir.empty(); dir = dir.parent_path()) {
      if (fs::is_directory(dir / "ui_scripts", ec)) return dir / "ui_scripts";
      if (dir == dir.parent_path()) break;
    }
    return fs::path();
  }();
  return cached;
}

// Text of the replacement for a game script name such as "Options\Main.lua" (empty = none).
std::string OverrideSource(const std::string& name) {
  const fs::path folder = ScriptsFolder();
  if (folder.empty() || name.empty() || name.find("..") != std::string::npos || name.find(':') != std::string::npos) return {};
  std::string relative = name;
  for (char& c : relative) if (c == '\\') c = '/';
  std::ifstream in(folder / fs::path(relative), std::ios::binary);
  if (!in) return {};
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}
}  // namespace

// Script loading: serve our own source when ui_scripts has a file with the same name.
REX_HOOK_RAW(sub_820A6898) {
  const uint32_t L = ctx.r3.u32, name_ptr = ctx.r4.u32;
  if (REXCVAR_GET(ui_mods)) {
    const std::string name = GuestString(base, name_ptr);
    const std::string source = OverrideSource(name);
    if (!source.empty()) {
      const std::string chunk = "@" + name;
      const uint32_t size = uint32_t(source.size());
      const uint32_t buffer = GuestAlloc(ctx, base, size + uint32_t(chunk.size()) + 1);
      if (buffer) {
        std::memcpy(Host(base, buffer), source.data(), size);
        std::memcpy(Host(base, buffer + size), chunk.c_str(), chunk.size() + 1);
        const uint32_t status = Call(sub_820A6708, ctx, base, L, buffer, size, buffer + size);
        if (status != 0) {
          const uint32_t message = Call(sub_820A4900, ctx, base, L, -1, 0);
          REXLOG_ERROR("[ui] {} failed to load: {}", name, GuestString(base, message));
        } else {
          REXLOG_INFO("[ui] loaded {} from ui_scripts", name);
        }
        GuestFree(ctx, base, buffer);
        ctx.r3.u64 = status;
        return;
      }
    }
  }
  ctx.r3.u64 = L;
  ctx.r4.u64 = name_ptr;
  __imp__sub_820A6898(ctx, base);
}

// Language load: add our own strings to the game's text table (and so to UIText).
REX_HOOK_RAW(sub_8209BC38) {
  const uint32_t loc = ctx.r3.u32, L = ctx.r4.u32;
  __imp__sub_8209BC38(ctx, base);
  if (!REXCVAR_GET(ui_mods) || !loc || !L) return;

  // The game's .ini text is UTF-16 little-endian with a byte-order mark and CR LF lines.
  const std::string text = bbr::SettingsStrings();
  std::string utf16 = "\xFF\xFE";
  for (unsigned char c : text) {
    if (c == '\n') utf16.append("\r\0", 2);
    utf16.push_back(char(c));
    utf16.push_back('\0');
  }
  constexpr uint32_t kFileStruct = 0x40;
  const uint32_t block = GuestAlloc(ctx, base, kFileStruct + uint32_t(utf16.size()));
  if (!block) return;
  std::memset(Host(base, block), 0, kFileStruct);
  std::memcpy(Host(base, block + kFileStruct), utf16.data(), utf16.size());
  *Host(base, block + 1) = 1;                            // contents are in memory
  Store32(base, block + 0x8, 0);                         // read position
  Store32(base, block + 0xC, block + kFileStruct);       // buffer
  Store32(base, block + 0x10, uint32_t(utf16.size()));   // size

  Call(sub_820A5010, ctx, base, L, kLuaGlobalsIndex, kUITextName);  // push UIText
  Call(sub_8209B8D0, ctx, base, loc, L, block);
  Call(sub_820A4348, ctx, base, L, -2);                             // pop it again
  GuestFree(ctx, base, block);
  REXLOG_INFO("[ui] added the Revolution strings to the game's text table");
}

// gcinfo("bbrr.<command>", key, value): the settings screen's line to the PC side.
REX_HOOK_RAW(sub_820A79C8) {
  const uint32_t L = ctx.r3.u32;
  if (Call(sub_820A45B0, ctx, base, L, 1) == kLuaTString) {
    const std::string command = GuestString(base, Call(sub_820A4900, ctx, base, L, 1, 0));
    if (command.rfind("bbrr.", 0) == 0) {
      auto argument = [&](int index) -> std::string {
        const uint32_t type = Call(sub_820A45B0, ctx, base, L, index);
        if (type != kLuaTString && type != 3 /* number */) return {};
        return GuestString(base, Call(sub_820A4900, ctx, base, L, index, 0));
      };
      const std::string key = argument(2), value = argument(3);
      const std::string result = bbr::SettingsCommand(command, key, value);
      const uint32_t buffer = GuestAlloc(ctx, base, uint32_t(result.size()) + 1);
      if (buffer) {
        std::memcpy(Host(base, buffer), result.c_str(), result.size() + 1);
        Call(sub_820A4CF8, ctx, base, L, buffer);
        GuestFree(ctx, base, buffer);
      } else {
        Call(sub_820A4CF8, ctx, base, L, 0);  // pushes nil
      }
      ctx.r3.u64 = 1;  // one result
      return;
    }
  }
  ctx.r3.u64 = L;
  __imp__sub_820A79C8(ctx, base);
}
