// Local leaderboards (R-502 / R-503).
//
// The game talks to Xbox Live leaderboards through XDK XOnline wrappers that are part of
// the recompiled code. ReXGlue v0.10.1 only logs XSessionWriteStats / XUserReadStats and
// has no XamUserCreateStatsEnumerator, so the game's leaderboard screens stay empty.
// We hook the wrappers and serve everything from a local score table instead:
//
//   sub_8215FA28  XSessionWriteStats(hSession, xuid, numViews, pViews, pOverlapped)
//   sub_820E76E0  XUserReadStats(titleId, numXuids, pXuids, numSpecs, pSpecs, pcb, pResults, pOv)
//   sub_820E77C8  XUserCreateStatsEnumeratorByRank(titleId, rankStart, numRows, numSpecs, pSpecs, pcb, phEnum)
//   sub_820E7828  XUserCreateStatsEnumeratorByXuid(titleId, xuidPivot, numRows, numSpecs, pSpecs, pcb, phEnum)
//   sub_820E7368  XEnumerate(hEnum, pBuffer, cbBuffer, pcItems, pOverlapped)  (ours only; others pass through)
//
// Leaderboard schema (from the game's embedded XLAST, 250 views): every view is ranked by
// LB_SCORE (property 0x20000001), descending. Column attribute ids map to properties:
//   65535 Rank, 65533 Gamer name, 65534 Score/rating, 1 Avg accuracy, 2 Grade, 3 Hit ratio,
//   4 BPM, 5 Lap, 6 Time lasted.
//
// Scores are saved to <user folder>/boomboomrocket/leaderboards.txt.

#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

REX_EXTERN(__imp__sub_8215FA28);
REX_EXTERN(__imp__sub_820E7368);

namespace bbr::lb {
namespace {

// ---- Xbox constants -------------------------------------------------------
constexpr uint32_t kErrSuccess = 0x0;
constexpr uint32_t kErrNoMoreFiles = 0x12;
constexpr uint32_t kErrInsufficientBuffer = 0x7A;
constexpr uint32_t kErrIoPending = 0x3E5;
constexpr uint8_t kTypeInt32 = 1, kTypeInt64 = 2, kTypeNull = 0xFF;
constexpr uint32_t kPropScore = 0x20000001;
constexpr uint16_t kColRank = 65535, kColGamerName = 65533, kColRating = 65534;

// XDK struct sizes (guest, big-endian).
constexpr uint32_t kSizeResults = 8;   // XUSER_STATS_READ_RESULTS {numViews, pViews}
constexpr uint32_t kSizeView = 16;     // XUSER_STATS_VIEW {viewId, totalRows, numRows, pRows}
constexpr uint32_t kSizeRow = 48;      // XUSER_STATS_ROW {xuid, rank, pad, rating, tag[16], numCols, pCols}
constexpr uint32_t kSizeColumn = 24;   // XUSER_STATS_COLUMN {u16 id, pad, XUSER_DATA}
constexpr uint32_t kSizeProperty = 24; // XUSER_PROPERTY {id, pad, XUSER_DATA}
constexpr uint32_t kSizeSpec = 136;    // XUSER_STATS_SPEC {viewId, numColumns, u16 columns[64]}

uint32_t ColumnToProperty(uint16_t column) {
  switch (column) {
    case kColRating: return kPropScore;
    case 1: return 0x30000006;  // LB_AVERAGE_ACCURACY (double)
    case 2: return 0x10000008;  // LB_GRADE (int32)
    case 3: return 0x30000007;  // LB_HIT_RATIO (double)
    case 4: return 0x30000003;  // LB_BPM (double)
    case 5: return 0x10000004;  // LB_LAP (int32)
    case 6: return 0x30000005;  // LB_TIME_LASTED (double)
    default: return 0;
  }
}

// ---- guest memory helpers ------------------------------------------------
uint8_t* g_base = nullptr;
uint32_t ld32(uint32_t a) { uint32_t v; std::memcpy(&v, g_base + a, 4); return __builtin_bswap32(v); }
uint64_t ld64(uint32_t a) { uint64_t v; std::memcpy(&v, g_base + a, 8); return __builtin_bswap64(v); }
uint16_t ld16(uint32_t a) { uint16_t v; std::memcpy(&v, g_base + a, 2); return __builtin_bswap16(v); }
void st8(uint32_t a, uint8_t v) { g_base[a] = v; }
void st16(uint32_t a, uint16_t v) { v = __builtin_bswap16(v); std::memcpy(g_base + a, &v, 2); }
void st32(uint32_t a, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(g_base + a, &v, 4); }
void st64(uint32_t a, uint64_t v) { v = __builtin_bswap64(v); std::memcpy(g_base + a, &v, 8); }

// ---- score table -----------------------------------------------------------
struct Value {
  uint8_t type = kTypeNull;
  uint64_t raw = 0;  // 8-byte union payload as stored in guest XUSER_DATA (host order)
};
struct Entry {
  uint64_t xuid = 0;
  int64_t rating = 0;
  uint64_t order = 0;  // first-come wins ties
  std::string gamertag;
  std::map<uint32_t, Value> props;
};

std::mutex g_mutex;
std::map<uint32_t, std::vector<Entry>> g_views;  // viewId -> entries
uint64_t g_order = 0;
bool g_loaded = false;

std::filesystem::path StorePath() {
  return rex::filesystem::GetUserFolder() / "boomboomrocket" / "leaderboards.txt";
}

void SortView(std::vector<Entry>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) {
    return a.rating != b.rating ? a.rating > b.rating : a.order < b.order;
  });
}

void Load() {
  if (g_loaded) return;
  g_loaded = true;
  std::ifstream in(StorePath());
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    Entry e;
    uint32_t view;
    ss >> std::dec >> view >> std::hex >> e.xuid >> std::dec >> e.rating >> e.order >> std::quoted(e.gamertag);
    std::string prop;
    while (ss >> prop) {  // id:type:raw (hex)
      unsigned id, type; unsigned long long raw;
      if (std::sscanf(prop.c_str(), "%x:%x:%llx", &id, &type, &raw) == 3)
        e.props[id] = Value{uint8_t(type), raw};
    }
    g_order = std::max(g_order, e.order + 1);
    g_views[view].push_back(std::move(e));
  }
  for (auto& [id, v] : g_views) SortView(v);
  REXLOG_INFO("[leaderboards] loaded {} views from {}", g_views.size(), StorePath().string());
}

void Save() {
  std::error_code ec;
  std::filesystem::create_directories(StorePath().parent_path(), ec);
  auto tmp = StorePath();
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << "# Boom Boom Rocket local leaderboards: view xuid(hex) score order \"gamertag\" prop:type:raw...\n";
    for (auto& [view, entries] : g_views) {
      for (auto& e : entries) {
        out << std::dec << view << ' ' << std::hex << e.xuid << ' ' << std::dec << e.rating << ' '
            << e.order << ' ' << std::quoted(e.gamertag);
        for (auto& [id, val] : e.props)
          out << ' ' << std::hex << id << ':' << unsigned(val.type) << ':' << val.raw;
        out << '\n';
      }
    }
  }
  std::filesystem::rename(tmp, StorePath(), ec);
  if (ec) REXLOG_WARN("[leaderboards] failed to save {}: {}", StorePath().string(), ec.message());
}

std::string GamertagFor(uint64_t xuid) {
  if (auto* ks = REX_KERNEL_STATE()) {
    if (auto* profile = ks->user_profile(); profile && profile->xuid() == xuid) {
      return profile->name().substr(0, 15);
    }
  }
  return "Player 2";
}

int64_t ValueAsInt64(const Value& v) {
  if (v.type == kTypeInt64) return int64_t(v.raw);
  if (v.type == kTypeInt32) return int32_t(uint32_t(v.raw >> 32));  // int32 sits in the first 4 bytes
  return 0;
}

// XUSER_DATA at guest address a: type @0, payload @8.
Value ReadData(uint32_t a) { return Value{g_base[a], ld64(a + 8)}; }
void WriteData(uint32_t a, const Value& v) {
  std::memset(g_base + a, 0, 16);
  st8(a, v.type);
  st64(a + 8, v.raw);
}
Value Int32Value(int32_t x) { return Value{kTypeInt32, uint64_t(uint32_t(x)) << 32}; }

void CompleteOverlapped(uint32_t overlapped, uint32_t result, uint32_t length) {
  if (auto* ks = REX_KERNEL_STATE()) ks->CompleteOverlappedImmediateEx(overlapped, result, 0, length);
}

// ---- result writer ---------------------------------------------------------
struct Spec {
  uint32_t view;
  std::vector<uint16_t> columns;
};
std::vector<Spec> ReadSpecs(uint32_t count, uint32_t ptr) {
  std::vector<Spec> specs;
  for (uint32_t i = 0; i < count && i < 64; ++i) {
    uint32_t s = ptr + i * kSizeSpec;
    Spec spec{ld32(s), {}};
    uint32_t n = std::min<uint32_t>(ld32(s + 4), 64);
    for (uint32_t c = 0; c < n; ++c) spec.columns.push_back(ld16(s + 8 + c * 2));
    specs.push_back(std::move(spec));
  }
  return specs;
}

struct OutRow {
  const Entry* entry;  // nullptr = no stats for this xuid
  uint64_t xuid;
  uint32_t rank;
};
using RowsPerView = std::vector<std::vector<OutRow>>;

uint32_t ResultsSize(const std::vector<Spec>& specs, const RowsPerView& rows) {
  uint32_t size = kSizeResults + uint32_t(specs.size()) * kSizeView;
  for (size_t v = 0; v < specs.size(); ++v)
    size += uint32_t(rows[v].size()) * (kSizeRow + uint32_t(specs[v].columns.size()) * kSizeColumn);
  return size;
}

void WriteResults(uint32_t out, const std::vector<Spec>& specs, const RowsPerView& rows) {
  std::memset(g_base + out, 0, ResultsSize(specs, rows));
  const uint32_t views = out + kSizeResults;
  st32(out + 0, uint32_t(specs.size()));
  st32(out + 4, specs.empty() ? 0 : views);
  uint32_t cursor = views + uint32_t(specs.size()) * kSizeView;
  for (size_t v = 0; v < specs.size(); ++v) {
    const uint32_t view = views + uint32_t(v) * kSizeView;
    const auto& spec = specs[v];
    auto it = g_views.find(spec.view);
    st32(view + 0, spec.view);
    st32(view + 4, it == g_views.end() ? 0 : uint32_t(it->second.size()));
    st32(view + 8, uint32_t(rows[v].size()));
    st32(view + 12, rows[v].empty() ? 0 : cursor);
    const uint32_t row_base = cursor;
    cursor += uint32_t(rows[v].size()) * kSizeRow;
    for (size_t r = 0; r < rows[v].size(); ++r) {
      const OutRow& orow = rows[v][r];
      const uint32_t row = row_base + uint32_t(r) * kSizeRow;
      st64(row + 0, orow.xuid);
      st32(row + 8, orow.entry ? orow.rank : 0);
      st64(row + 16, orow.entry ? uint64_t(orow.entry->rating) : 0);
      if (orow.entry) {
        std::string tag = orow.entry->gamertag.substr(0, 15);
        std::memcpy(g_base + row + 24, tag.c_str(), tag.size() + 1);
      }
      st32(row + 40, uint32_t(spec.columns.size()));
      st32(row + 44, spec.columns.empty() ? 0 : cursor);
      for (size_t c = 0; c < spec.columns.size(); ++c) {
        const uint32_t col = cursor + uint32_t(c) * kSizeColumn;
        const uint16_t id = spec.columns[c];
        st16(col + 0, id);
        Value val;  // NULL unless we have it
        if (orow.entry) {
          if (id == kColRank) {
            val = Int32Value(int32_t(orow.rank));
          } else if (id == kColRating) {
            val = Value{kTypeInt64, uint64_t(orow.entry->rating)};
          } else if (uint32_t prop = ColumnToProperty(id)) {
            auto p = orow.entry->props.find(prop);
            if (p != orow.entry->props.end()) val = p->second;
          }
          // kColGamerName: the name is in the row's szGamertag; the column stays NULL.
        }
        WriteData(col + 8, val);
      }
      cursor += uint32_t(spec.columns.size()) * kSizeColumn;
    }
  }
}

// ---- enumerators -----------------------------------------------------------
struct Enumerator {
  bool by_xuid;
  uint64_t pivot;  // rank start (1-based) or xuid
  uint32_t num_rows;
  std::vector<Spec> specs;
  bool done = false;
};
std::map<uint32_t, Enumerator> g_enums;
uint32_t g_next_handle = 0xBB000001;  // fake handles, outside the kernel's 0xF8xxxxxx range

RowsPerView RowsForEnumerator(const Enumerator& e) {
  RowsPerView rows(e.specs.size());
  for (size_t v = 0; v < e.specs.size(); ++v) {
    auto it = g_views.find(e.specs[v].view);
    if (it == g_views.end()) continue;
    const auto& entries = it->second;
    size_t start = 0;
    if (e.by_xuid) {
      auto pos = std::find_if(entries.begin(), entries.end(),
                              [&](const Entry& x) { return x.xuid == e.pivot; });
      size_t idx = pos == entries.end() ? 0 : size_t(pos - entries.begin());
      start = idx >= e.num_rows / 2 ? idx - e.num_rows / 2 : 0;  // centre the page on the player
    } else {
      start = e.pivot > 0 ? size_t(e.pivot - 1) : 0;
    }
    for (size_t i = start; i < entries.size() && rows[v].size() < e.num_rows; ++i)
      rows[v].push_back(OutRow{&entries[i], entries[i].xuid, uint32_t(i + 1)});
  }
  return rows;
}

uint32_t EnumeratorBufferSize(const Enumerator& e) {
  // Worst case: num_rows rows in every view.
  RowsPerView rows(e.specs.size());
  for (size_t v = 0; v < e.specs.size(); ++v) rows[v].resize(e.num_rows, OutRow{nullptr, 0, 0});
  return ResultsSize(e.specs, rows);
}

uint32_t CreateEnumerator(bool by_xuid, uint64_t pivot, uint32_t num_rows, uint32_t num_specs,
                          uint32_t specs_ptr, uint32_t pcb, uint32_t phandle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  Load();
  Enumerator e{by_xuid, pivot, std::min<uint32_t>(num_rows, 100), ReadSpecs(num_specs, specs_ptr)};
  const uint32_t handle = g_next_handle++;
  if (pcb) st32(pcb, EnumeratorBufferSize(e));
  if (phandle) st32(phandle, handle);
  REXLOG_DEBUG("[leaderboards] enumerator {:08X}: {} pivot={} rows={} views={}", handle,
               by_xuid ? "ByXuid" : "ByRank", pivot, e.num_rows, e.specs.size());
  g_enums.emplace(handle, std::move(e));
  return kErrSuccess;
}

}  // namespace
}  // namespace bbr::lb

using namespace bbr::lb;

// XSessionWriteStats(hSession, xuid, numViews, pViews, pOverlapped)
REX_HOOK_RAW(sub_8215FA28) {
  g_base = base;
  const uint64_t xuid = ctx.r4.u64;
  const uint32_t num_views = ctx.r5.u32, views = ctx.r6.u32;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Load();
    for (uint32_t i = 0; i < num_views && i < 64; ++i) {
      const uint32_t vp = views + i * 12;  // XSESSION_VIEW_PROPERTIES {viewId, numProps, pProps}
      const uint32_t view_id = ld32(vp), num_props = std::min<uint32_t>(ld32(vp + 4), 64);
      const uint32_t props = ld32(vp + 8);
      Entry incoming;
      incoming.xuid = xuid;
      incoming.gamertag = GamertagFor(xuid);
      for (uint32_t p = 0; p < num_props; ++p) {
        const uint32_t pp = props + p * kSizeProperty;
        incoming.props[ld32(pp)] = ReadData(pp + 8);
      }
      auto score = incoming.props.find(kPropScore);
      if (score == incoming.props.end()) continue;
      incoming.rating = ValueAsInt64(score->second);
      const int64_t new_score = incoming.rating;
      auto& entries = g_views[view_id];
      auto existing = std::find_if(entries.begin(), entries.end(),
                                   [&](const Entry& e) { return e.xuid == xuid; });
      if (existing == entries.end()) {
        incoming.order = g_order++;
        entries.push_back(std::move(incoming));
      } else if (incoming.rating > existing->rating) {  // keep the personal best
        incoming.order = existing->order;
        *existing = std::move(incoming);
      } else {
        continue;
      }
      SortView(entries);
      REXLOG_INFO("[leaderboards] view {}: new best {} for {:016X}", view_id, new_score, xuid);
    }
    Save();
  }
  // Let the original finish the call (completes the overlapped through the XGI app).
  __imp__sub_8215FA28(ctx, base);
}

// XUserReadStats(titleId, numXuids, pXuids, numSpecs, pSpecs, pcbResults, pResults, pOverlapped)
REX_HOOK_RAW(sub_820E76E0) {
  g_base = base;
  const uint32_t num_xuids = std::min<uint32_t>(ctx.r4.u32, 100), xuids = ctx.r5.u32;
  const uint32_t num_specs = ctx.r6.u32, specs_ptr = ctx.r7.u32;
  const uint32_t pcb = ctx.r8.u32, results = ctx.r9.u32, overlapped = ctx.r10.u32;
  std::lock_guard<std::mutex> lock(g_mutex);
  Load();
  auto specs = ReadSpecs(num_specs, specs_ptr);
  RowsPerView rows(specs.size());
  for (size_t v = 0; v < specs.size(); ++v) {
    auto it = g_views.find(specs[v].view);
    for (uint32_t i = 0; i < num_xuids; ++i) {
      const uint64_t xuid = ld64(xuids + i * 8);
      OutRow row{nullptr, xuid, 0};
      if (it != g_views.end()) {
        for (size_t r = 0; r < it->second.size(); ++r)
          if (it->second[r].xuid == xuid) row = OutRow{&it->second[r], xuid, uint32_t(r + 1)};
      }
      rows[v].push_back(row);
    }
  }
  const uint32_t needed = ResultsSize(specs, rows);
  if (!results || !pcb || ld32(pcb) < needed) {
    if (pcb) st32(pcb, needed);
    ctx.r3.u64 = kErrInsufficientBuffer;
    return;
  }
  WriteResults(results, specs, rows);
  if (overlapped) {
    CompleteOverlapped(overlapped, kErrSuccess, 0);
    ctx.r3.u64 = kErrIoPending;
  } else {
    ctx.r3.u64 = kErrSuccess;
  }
}

// XUserCreateStatsEnumeratorByRank(titleId, rankStart, numRows, numSpecs, pSpecs, pcbBuffer, phEnum)
REX_HOOK_RAW(sub_820E77C8) {
  g_base = base;
  ctx.r3.u64 = CreateEnumerator(false, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
                                ctx.r9.u32);
}

// XUserCreateStatsEnumeratorByXuid(titleId, xuidPivot, numRows, numSpecs, pSpecs, pcbBuffer, phEnum)
REX_HOOK_RAW(sub_820E7828) {
  g_base = base;
  ctx.r3.u64 = CreateEnumerator(true, ctx.r4.u64, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32,
                                ctx.r9.u32);
}

// XEnumerate(hEnum, pBuffer, cbBuffer, pcItemsReturned, pOverlapped)
REX_HOOK_RAW(sub_820E7368) {
  g_base = base;
  const uint32_t handle = ctx.r3.u32;
  std::unique_lock<std::mutex> lock(g_mutex);
  auto it = g_enums.find(handle);
  if (it == g_enums.end()) {  // not a leaderboard enumerator (content, etc.)
    lock.unlock();
    __imp__sub_820E7368(ctx, base);
    return;
  }
  const uint32_t buffer = ctx.r4.u32, cb = ctx.r5.u32, pitems = ctx.r6.u32, overlapped = ctx.r7.u32;
  Enumerator& e = it->second;
  uint32_t result = kErrNoMoreFiles, items = 0;
  if (!e.done) {
    auto rows = RowsForEnumerator(e);
    if (cb < ResultsSize(e.specs, rows)) {
      result = kErrInsufficientBuffer;
    } else {
      WriteResults(buffer, e.specs, rows);
      result = kErrSuccess;
      items = 1;
      e.done = true;
    }
  }
  if (pitems) st32(pitems, items);
  if (overlapped) {
    CompleteOverlapped(overlapped, result, items);
    ctx.r3.u64 = kErrIoPending;
  } else {
    ctx.r3.u64 = result;
  }
}
