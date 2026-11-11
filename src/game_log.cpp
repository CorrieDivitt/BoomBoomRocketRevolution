// The game's own debug output (--game_debug_log=true).
//
// The retail build still calls its debug printf, sub_820616C8(format, ...), but the
// function body was compiled out (it just returns; the same empty function is also used
// as a placeholder in vtables). With the cvar on we format those messages into our log,
// which shows what the game thinks while loading content, DLC packs, leaderboards, etc.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

REXCVAR_DEFINE_BOOL(game_debug_log, false, "BBR", "Write the game's built-in debug messages to the log");

REX_EXTERN(__imp__sub_820616C8);

namespace {
constexpr uint32_t kRdataStart = 0x82000000, kRdataEnd = 0x82060000;  // format strings live here
constexpr uint32_t kImageEnd = 0x822EE000;

bool Mapped(uint32_t addr) {
  // Image, heap and stack ranges a string argument can realistically point into.
  return (addr >= 0x82000000 && addr < kImageEnd) || (addr >= 0x40000000 && addr < 0x80000000) ||
         (addr >= 0x00010000 && addr < 0x40000000);
}

std::string GuestString(uint8_t* base, uint32_t addr, bool wide) {
  std::string out;
  if (!Mapped(addr)) return "(bad pointer)";
  for (int i = 0; i < 200; ++i) {
    uint32_t c;
    if (wide) {
      c = (uint32_t(base[addr + i * 2]) << 8) | base[addr + i * 2 + 1];
    } else {
      c = base[addr + i];
    }
    if (c == 0) break;
    out.push_back(c >= 32 && c < 127 ? char(c) : '?');
  }
  return out;
}
}  // namespace

REX_HOOK_RAW(sub_820616C8) {
  if (REXCVAR_GET(game_debug_log)) {
    const uint32_t fmt = ctx.r3.u32;
    if (fmt >= kRdataStart && fmt < kRdataEnd) {
      const uint64_t args[7] = {ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, ctx.r7.u64,
                                ctx.r8.u64, ctx.r9.u64, ctx.r10.u64};
      int next = 0;
      std::string out;
      bool plausible = true;
      for (int i = 0; i < 400 && plausible; ++i) {
        const uint8_t c = base[fmt + i];
        if (c == 0) break;
        if (c != '%') {
          if (c == '\n' || c == '\r') { out.push_back(' '); continue; }
          if (c < 32 || c >= 127) { plausible = false; break; }
          out.push_back(char(c));
          continue;
        }
        // Skip flags / width / precision / length modifiers.
        uint8_t spec = base[fmt + ++i];
        while (spec && std::strchr("-+ #0123456789.lh", spec)) spec = base[fmt + ++i];
        if (spec == '%') { out.push_back('%'); continue; }
        const uint64_t arg = next < 7 ? args[next++] : 0;
        char buf[64];
        switch (spec) {
          case 'd': case 'i': std::snprintf(buf, sizeof(buf), "%d", int32_t(arg)); out += buf; break;
          case 'u': std::snprintf(buf, sizeof(buf), "%u", uint32_t(arg)); out += buf; break;
          case 'x': case 'X': case 'p': std::snprintf(buf, sizeof(buf), "%X", uint32_t(arg)); out += buf; break;
          case 'c': out.push_back(char(arg)); break;
          case 'f': case 'g': case 'e': {
            double d;
            std::memcpy(&d, &arg, sizeof(d));
            std::snprintf(buf, sizeof(buf), "%.4f", d);
            out += buf;
            break;
          }
          case 's': out += GuestString(base, uint32_t(arg), false); break;
          case 'S': out += GuestString(base, uint32_t(arg), true); break;
          default: out += "%?"; break;
        }
        if (spec == 0) break;
      }
      if (plausible && out.size() >= 4) REXLOG_INFO("[game] {}", out);
    }
  }
  __imp__sub_820616C8(ctx, base);
}
