// Boom Boom Rocket Revolution launcher: checking and installing the player's files.
// See launcher_core.h.

#include "launcher_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace bbrr {
namespace {

uint32_t BE32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t BE16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t LE24(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }

std::string Lower(std::string v) {
  for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
  return v;
}

std::string Hex32(uint32_t v) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", v);
  return text;
}

std::string PathText(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

bool ReadFile(const fs::path& path, std::vector<uint8_t>* out, std::string* error, uint64_t limit = 0) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "could not open " + PathText(path.filename());
    return false;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  in.seekg(0, std::ios::beg);
  if (size < 0) {
    if (error) *error = "could not read " + PathText(path.filename());
    return false;
  }
  uint64_t want = uint64_t(size);
  if (limit && want > limit) want = limit;
  out->resize(size_t(want));
  if (want && !in.read(reinterpret_cast<char*>(out->data()), std::streamsize(want))) {
    if (error) *error = "could not read " + PathText(path.filename());
    return false;
  }
  return true;
}

// Hashes a file in pieces, counting what it read into the progress.
bool HashFile(const fs::path& path, std::string* hex, Progress* progress) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  Sha256 sha;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    sha.Update(buffer.data(), size_t(got));
    if (progress) {
      progress->done += uint64_t(got);
      if (progress->cancel) return false;
    }
  }
  *hex = sha.HexDigest();
  return true;
}

bool CopyWithProgress(const fs::path& from, const fs::path& to, Progress* progress, std::string* error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!in || !out) {
    if (error) *error = "could not copy " + PathText(from.filename());
    return false;
  }
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize got = in.gcount();
    if (got <= 0) break;
    if (!out.write(buffer.data(), got)) {
      if (error) *error = "could not write " + PathText(to.filename()) + " (is the disk full?)";
      return false;
    }
    if (progress) {
      progress->done += uint64_t(got);
      if (progress->cancel) {
        if (error) *error = "cancelled";
        return false;
      }
    }
  }
  return true;
}

bool WriteFile(const fs::path& to, const std::vector<uint8_t>& data, std::string* error) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  std::ofstream out(to, std::ios::binary | std::ios::trunc);
  if (!out || (!data.empty() && !out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size())))) {
    if (error) *error = "could not write " + PathText(to.filename()) + " (is the disk full?)";
    return false;
  }
  return true;
}

std::string Utf16BeToUtf8(const uint8_t* p, size_t bytes) {
  std::string out;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    uint32_t c = BE16(p + i);
    if (c == 0) break;
    if (c >= 0xD800 && c <= 0xDBFF && i + 3 < bytes) {
      const uint32_t low = BE16(p + i + 2);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        i += 2;
      }
    }
    if (c < 0x80) {
      out.push_back(char(c));
    } else if (c < 0x800) {
      out.push_back(char(0xC0 | (c >> 6)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(char(0xE0 | (c >> 12)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (c >> 18)));
      out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(char(0x80 | (c & 0x3F)));
    }
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '\0')) out.pop_back();
  return out;
}

const char* kNotThisGame = "This isn't Boom Boom Rocket";
}  // namespace

// ---------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4).
namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

Sha256::Sha256() {
  static constexpr uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::memcpy(h_, init, sizeof(h_));
}

void Sha256::Block(const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = BE32(p + 4 * i);
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
  h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::Update(const void* data, size_t size) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  length_ += size;
  if (buffered_) {
    const size_t take = std::min(size, 64 - buffered_);
    std::memcpy(buffer_ + buffered_, p, take);
    buffered_ += take;
    p += take;
    size -= take;
    if (buffered_ == 64) {
      Block(buffer_);
      buffered_ = 0;
    }
  }
  while (size >= 64) {
    Block(p);
    p += 64;
    size -= 64;
  }
  if (size) {
    std::memcpy(buffer_, p, size);
    buffered_ = size;
  }
}

std::string Sha256::HexDigest() {
  const uint64_t bits = length_ * 8;
  const uint8_t one = 0x80, zero = 0;
  Update(&one, 1);
  while (buffered_ != 56) Update(&zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
  Update(len, 8);
  std::string out;
  char text[9];
  for (uint32_t v : h_) {
    std::snprintf(text, sizeof(text), "%08x", v);
    out += text;
  }
  return out;
}

std::string Sha256Hex(const void* data, size_t size) {
  Sha256 sha;
  sha.Update(data, size);
  return sha.HexDigest();
}

void Progress::SetStage(const std::string& stage) {
  std::lock_guard<std::mutex> lock(mutex_);
  stage_ = stage;
}
std::string Progress::Stage() {
  std::lock_guard<std::mutex> lock(mutex_);
  return stage_;
}

std::string FormatBytes(uint64_t bytes) {
  char text[32];
  if (bytes >= (1ull << 30)) {
    std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
  } else if (bytes >= (1ull << 20)) {
    std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / double(1ull << 20));
  } else if (bytes >= 1024) {
    std::snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1024.0);
  } else {
    std::snprintf(text, sizeof(text), "%llu bytes", static_cast<unsigned long long>(bytes));
  }
  return text;
}

// ---------------------------------------------------------------------------------------
const std::vector<ManifestFile>& GameManifest() {
  static const std::vector<ManifestFile> files = {
#include "game_files.inc"
  };
  return files;
}

const ManifestFile* ManifestXex() {
  for (const ManifestFile& f : GameManifest()) {
    if (Lower(f.path) == "default.xex") return &f;
  }
  return nullptr;
}

XexInfo ReadXexInfo(const uint8_t* data, size_t size) {
  XexInfo info;
  if (size < 0x18 || std::memcmp(data, "XEX2", 4) != 0) {
    info.error = "not an Xbox 360 program (no XEX2 header)";
    return info;
  }
  const uint32_t count = BE32(data + 0x14);
  if (count > 256 || 0x18 + size_t(count) * 8 > size) {
    info.error = "the program header is damaged";
    return info;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = data + 0x18 + i * 8;
    if (BE32(e) != 0x00040006) continue;  // execution info
    const uint32_t at = BE32(e + 4);
    if (size_t(at) + 16 > size) break;
    info.media_id = BE32(data + at);
    info.version = BE32(data + at + 4);
    info.base_version = BE32(data + at + 8);
    info.title_id = BE32(data + at + 12);
    info.ok = true;
    return info;
  }
  info.error = "the program header has no title information";
  return info;
}

// ---------------------------------------------------------------------------------------
// STFS packages. Layout as in tools/stfs/stfs.py (which unpacked the DLC packs for R-111).
bool Package::Open(const fs::path& path, std::string* error) {
  path_ = path;
  entries.clear();
  data_.clear();
  std::vector<uint8_t> head;
  if (!ReadFile(path, &head, error, 0x1000)) return false;
  if (head.size() < 0x500) {
    if (error) *error = "too small to be a package";
    return false;
  }
  magic.assign(reinterpret_cast<const char*>(head.data()), 4);
  if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") {
    if (error) *error = "not an Xbox 360 package";
    return false;
  }
  header_size_ = BE32(head.data() + 0x340);
  content_type = BE32(head.data() + 0x344);
  title_id = BE32(head.data() + 0x360);
  display_name = Utf16BeToUtf8(head.data() + 0x411, 0x80);
  base_ = (header_size_ + 0xFFF) & 0xFFFFF000u;
  const uint8_t separation = head[0x37B];
  file_table_blocks_ = uint16_t(head[0x37C] | (head[0x37D] << 8));
  file_table_block_ = LE24(head.data() + 0x37E);
  shift_ = ((base_ >> 12) == 0xB || (separation & 1) == 1) ? 0 : 1;
  return true;
}

bool Package::ReadAll(std::string* error) {
  if (!ReadFile(path_, &data_, error)) return false;
  return ReadFileTable(error);
}

uint64_t Package::BlockOffset(uint32_t b) const {
  uint64_t r = (uint64_t((b + 0xAA) / 0xAA) << shift_) + b;
  if (b >= 0xAA) {
    r += uint64_t((b + 0x70E4) / 0x70E4) << shift_;
    if (b >= 0x70E4) r += uint64_t(1) << shift_;
  }
  return uint64_t(base_) + (r << 12);
}

uint64_t Package::HashOffset(uint32_t b) const {
  const uint64_t step = shift_ == 0 ? 0xAB : 0xAC;
  uint64_t n = uint64_t(b / 0xAA) * step;
  if (b >= 0xAA) n += uint64_t((b / 0x70E4) + 1) << shift_;
  if (b >= 0x70E4) n += uint64_t(1) << shift_;
  return uint64_t(base_) + (n << 12) + uint64_t(b % 0xAA) * 0x18;
}

bool Package::NextBlock(uint32_t block, uint32_t* next) const {
  const uint64_t at = HashOffset(block);
  if (at + 0x18 > data_.size()) return false;
  const uint8_t* e = data_.data() + at;
  *next = (uint32_t(e[0x15]) << 16) | (uint32_t(e[0x16]) << 8) | e[0x17];
  return true;
}

bool Package::ReadFileTable(std::string* error) {
  entries.clear();
  if (file_table_blocks_ == 0 || file_table_blocks_ > 1024) {
    if (error) *error = "its file list could not be read";
    return false;
  }
  std::vector<uint8_t> table;
  uint32_t block = file_table_block_;
  for (uint32_t i = 0; i < file_table_blocks_; ++i) {
    const uint64_t at = BlockOffset(block);
    if (at + 0x1000 > data_.size()) {
      if (error) *error = "its file list could not be read";
      return false;
    }
    table.insert(table.end(), data_.begin() + std::ptrdiff_t(at), data_.begin() + std::ptrdiff_t(at + 0x1000));
    if (i + 1 < file_table_blocks_ && !NextBlock(block, &block)) {
      if (error) *error = "its file list could not be read";
      return false;
    }
  }
  std::vector<std::string> names;
  for (size_t i = 0; i + 0x40 <= table.size(); i += 0x40) {
    const uint8_t* e = table.data() + i;
    if (e[0] == 0) break;
    Entry entry;
    const uint8_t flags = e[0x28];
    std::string name(reinterpret_cast<const char*>(e), flags & 0x3F);
    entry.directory = (flags & 0x80) != 0;
    entry.consecutive = (flags & 0x40) != 0;
    entry.blocks = LE24(e + 0x29);
    entry.start = LE24(e + 0x2F);
    entry.parent = BE16(e + 0x32);
    entry.size = BE32(e + 0x34);
    if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name == "." || name == "..") {
      if (error) *error = "its file list is damaged";
      entries.clear();
      return false;
    }
    names.push_back(name);
    entries.push_back(entry);
  }
  // Full paths through the parent folders (a bad parent chain counts as damage).
  for (size_t i = 0; i < entries.size(); ++i) {
    std::string path = names[i];
    uint16_t parent = entries[i].parent;
    for (int depth = 0; parent != 0xFFFF; ++depth) {
      if (parent >= entries.size() || depth > 32) {
        if (error) *error = "its file list is damaged";
        entries.clear();
        return false;
      }
      path = names[parent] + "/" + path;
      parent = entries[parent].parent;
    }
    entries[i].path = path;
  }
  if (entries.empty()) {
    if (error) *error = "it holds no files";
    return false;
  }
  return true;
}

bool Package::Extract(const Entry& entry, std::vector<uint8_t>* out, std::string* error) const {
  out->clear();
  if (entry.directory) return true;
  if (uint64_t(entry.blocks) * 0x1000 < entry.size) {
    if (error) *error = entry.path + " is damaged in the package";
    return false;
  }
  out->reserve(entry.size);
  uint32_t block = entry.start;
  for (uint32_t i = 0; i < entry.blocks && out->size() < entry.size; ++i) {
    const uint64_t at = BlockOffset(block);
    const size_t take = std::min<size_t>(0x1000, entry.size - out->size());
    if (at + take > data_.size()) {
      if (error) *error = entry.path + " is cut short in the package";
      return false;
    }
    out->insert(out->end(), data_.begin() + std::ptrdiff_t(at), data_.begin() + std::ptrdiff_t(at + take));
    if (entry.consecutive) {
      ++block;
    } else if (i + 1 < entry.blocks && !NextBlock(block, &block)) {
      if (error) *error = entry.path + " is damaged in the package";
      return false;
    }
  }
  if (out->size() != entry.size) {
    if (error) *error = entry.path + " is damaged in the package";
    return false;
  }
  return true;
}

const Package::Entry* Package::Find(const std::string& path) const {
  const std::string want = Lower(path);
  for (const Entry& e : entries) {
    if (Lower(e.path) == want) return &e;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------------------
namespace {
std::string VersionText(uint32_t version) {
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", version >> 28, (version >> 24) & 0xF, (version >> 8) & 0xFFFF,
                version & 0xFF);
  return text;
}

// The default.xex checks shared by both kinds of copy.
bool CheckXex(const std::vector<uint8_t>& xex, GameSource* s) {
  const XexInfo info = ReadXexInfo(xex.data(), xex.size());
  if (!info.ok) {
    s->errors.push_back("default.xex: " + info.error);
    return false;
  }
  if (info.title_id != kTitleId) {
    s->errors.push_back(std::string(kNotThisGame) + ": default.xex belongs to title " + Hex32(info.title_id) +
                        " (Boom Boom Rocket is " + Hex32(kTitleId) + ")");
    return false;
  }
  s->xex_version = info.version;
  const ManifestFile* known = ManifestXex();
  s->known_version = known && Sha256Hex(xex.data(), xex.size()) == known->sha256;
  if (!s->known_version) {
    s->warnings.push_back("This default.xex (version " + VersionText(info.version) +
                          ") is not the one the port was made from. It may not work: the port is tested with "
                          "the retail Xbox Live Arcade release.");
  }
  return true;
}

void SummariseMismatch(GameSource* s, const std::vector<std::string>& missing, const std::vector<std::string>& differ) {
  auto list = [](const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size() && i < 5; ++i) out += (i ? ", " : "") + v[i];
    if (v.size() > 5) out += " and " + std::to_string(v.size() - 5) + " more";
    return out;
  };
  if (!missing.empty()) {
    s->errors.push_back(std::to_string(missing.size()) + " of the game's files are missing: " + list(missing) +
                        ". Pick the default.xex inside the complete game folder.");
  }
  if (!differ.empty()) {
    s->warnings.push_back(std::to_string(differ.size()) + " file(s) differ from the tested copy (modified or "
                          "damaged?): " + list(differ));
  }
}
}  // namespace

GameSource CheckGameSource(const fs::path& chosen_in, Progress* progress) {
  GameSource s;
  s.chosen = chosen_in;
  std::error_code ec;
  fs::path chosen = chosen_in;
  if (fs::is_directory(chosen, ec)) chosen /= "default.xex";
  if (!fs::is_regular_file(chosen, ec)) {
    s.errors.push_back("Nothing to read at " + PathText(chosen_in) + ".");
    s.summary = "Not found";
    return s;
  }
  std::vector<uint8_t> head;
  std::string error;
  if (!ReadFile(chosen, &head, &error, 4)) {
    s.errors.push_back("Could not read the file: " + error + ".");
    s.summary = "Could not read the file";
    return s;
  }
  const std::string magic = head.size() == 4 ? std::string(head.begin(), head.end()) : std::string();
  const auto& manifest = GameManifest();
  std::vector<std::string> missing, differ;

  if (magic == "XEX2") {
    // The unpacked game: default.xex with the rest of the files around it.
    s.kind = SourceKind::kFolder;
    s.folder = chosen.parent_path();
    std::vector<uint8_t> xex;
    if (!ReadFile(chosen, &xex, &error)) {
      s.errors.push_back("Could not read default.xex: " + error + ".");
      s.summary = "Could not read the file";
      return s;
    }
    if (Lower(PathText(chosen.filename())) != "default.xex") {
      s.warnings.push_back("The program is usually called default.xex; this one is " + PathText(chosen.filename()) +
                           ". It is installed as default.xex.");
    }
    if (!CheckXex(xex, &s)) {
      s.summary = "Not Boom Boom Rocket";
      return s;
    }
    uint64_t total = 0;
    for (const ManifestFile& f : manifest) total += f.size;
    if (progress) {
      progress->total = total;
      progress->done = 0;
      progress->SetStage("Checking the game files");
    }
    for (const ManifestFile& f : manifest) {
      if (progress && progress->cancel) break;
      const bool is_xex = Lower(f.path) == "default.xex";
      const fs::path file = is_xex ? chosen : s.folder / FromUtf8(f.path);
      const uint64_t size = fs::is_regular_file(file, ec) ? fs::file_size(file, ec) : UINT64_MAX;
      if (size == UINT64_MAX) {
        missing.push_back(f.path);
        if (progress) progress->done += f.size;
        continue;
      }
      s.bytes += size;
      if (!s.known_version || size != f.size) {  // a different version: no hashes to compare against
        if (size != f.size && s.known_version) differ.push_back(f.path);
        if (progress) progress->done += f.size;
        continue;
      }
      std::string hex;
      if (!HashFile(file, &hex, progress)) {
        if (progress && progress->cancel) break;
        missing.push_back(f.path);
        continue;
      }
      if (hex != f.sha256) differ.push_back(f.path);
    }
    if (!s.known_version) {
      // Copy the whole folder the program is in.
      s.bytes = 0;
      for (auto it = fs::recursive_directory_iterator(s.folder, ec); !ec && it != fs::recursive_directory_iterator();
           it.increment(ec)) {
        if (it->is_regular_file(ec)) s.bytes += it->file_size(ec);
      }
    }
  } else if (magic == "LIVE" || magic == "PIRS" || magic == "CON ") {
    // The original package, as downloaded from Xbox Live.
    s.kind = SourceKind::kPackage;
    Package package;
    if (!package.Open(chosen, &error)) {
      s.errors.push_back("Could not read the package: " + error + ".");
      s.summary = "Could not read the package";
      return s;
    }
    if (package.title_id != kTitleId) {
      s.errors.push_back(std::string(kNotThisGame) + ": the package belongs to title " + Hex32(package.title_id) +
                         (package.display_name.empty() ? std::string() : " (\"" + package.display_name + "\")") + ".");
      s.summary = "Not Boom Boom Rocket";
      return s;
    }
    if (package.content_type == kContentMarketplace) {
      s.errors.push_back("This is a DLC pack (\"" + package.display_name +
                         "\"), not the game. Add it under DLC below, and pick the game itself here.");
      s.summary = "This is DLC, not the game";
      return s;
    }
    if (package.content_type != kContentArcade) {
      s.errors.push_back("This package is not the Xbox Live Arcade game (content type " +
                         Hex32(package.content_type) + ").");
      s.summary = "Not the game package";
      return s;
    }
    if (progress) progress->SetStage("Reading the package");
    if (!package.ReadAll(&error)) {
      s.errors.push_back("Could not read the package: " + error + ".");
      s.summary = "Could not read the package";
      return s;
    }
    const Package::Entry* xex_entry = package.Find("default.xex");
    std::vector<uint8_t> xex;
    if (!xex_entry || !package.Extract(*xex_entry, &xex, &error)) {
      s.errors.push_back("The package has no readable default.xex.");
      s.summary = "Damaged package";
      return s;
    }
    if (!CheckXex(xex, &s)) {
      s.summary = "Not Boom Boom Rocket";
      return s;
    }
    uint64_t total = 0;
    for (const auto& e : package.entries) total += e.directory ? 0 : e.size;
    if (progress) {
      progress->total = total;
      progress->done = 0;
      progress->SetStage("Checking the game files");
    }
    for (const auto& e : package.entries) {
      if (!e.directory) s.bytes += e.size;
    }
    std::vector<uint8_t> data;
    for (const ManifestFile& f : manifest) {
      if (progress && progress->cancel) break;
      const Package::Entry* e = package.Find(f.path);
      if (!e) {
        missing.push_back(f.path);
        continue;
      }
      if (!package.Extract(*e, &data, &error)) {
        s.errors.push_back(error + ".");
        continue;
      }
      if (progress) progress->done += e->size;
      if (s.known_version && Sha256Hex(data.data(), data.size()) != f.sha256) differ.push_back(f.path);
    }
    if (progress) progress->done = progress->total.load();
  } else {
    s.errors.push_back("This file is neither default.xex nor an Xbox 360 game package. Pick default.xex in the "
                       "unpacked game folder, or the game's package file as it came from the console.");
    s.summary = "Not a game file";
    return s;
  }

  if (progress && progress->cancel) {
    s.errors.push_back("The check was cancelled.");
    s.summary = "Cancelled";
    return s;
  }
  SummariseMismatch(&s, missing, differ);
  s.ok = s.errors.empty();
  if (!s.ok) {
    s.summary = "Can't be installed";
  } else if (s.known_version && s.warnings.empty()) {
    s.summary = "Boom Boom Rocket, verified (" + std::to_string(manifest.size()) + " files, " + FormatBytes(s.bytes) + ")";
  } else {
    s.summary = "Boom Boom Rocket (" + FormatBytes(s.bytes) + "), with warnings";
  }
  return s;
}

bool InstallGame(const GameSource& source, const fs::path& install_dir, Progress* progress, std::string* error) {
  std::error_code ec;
  const fs::path target = install_dir / kGameFolder;
  if (!source.ok) {
    if (error) *error = "the game files have not been checked";
    return false;
  }
  if (source.kind == SourceKind::kFolder && fs::exists(target, ec) && fs::equivalent(source.folder, target, ec)) {
    return true;  // already where it belongs
  }
  const fs::path part = install_dir / (std::string(kGameFolder) + ".part");
  fs::remove_all(part, ec);
  if (progress) {
    progress->done = 0;
    progress->total = source.bytes;
    progress->SetStage("Installing the game files");
  }

  if (source.kind == SourceKind::kFolder) {
    if (source.known_version) {
      for (const ManifestFile& f : GameManifest()) {
        const bool is_xex = Lower(f.path) == "default.xex";
        const fs::path from = is_xex && fs::is_regular_file(source.chosen, ec) ? source.chosen : source.folder / FromUtf8(f.path);
        if (!CopyWithProgress(from, part / FromUtf8(f.path), progress, error)) {
          fs::remove_all(part, ec);
          return false;
        }
      }
    } else {
      for (auto it = fs::recursive_directory_iterator(source.folder, ec);
           !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), source.folder, ec);
        if (ec || rel.empty() || *rel.begin() == "..") continue;
        if (!CopyWithProgress(it->path(), part / rel, progress, error)) {
          fs::remove_all(part, ec);
          return false;
        }
      }
      // The program the player picked becomes default.xex.
      if (fs::is_regular_file(source.chosen, ec) && Lower(PathText(source.chosen.filename())) != "default.xex") {
        if (!CopyWithProgress(source.chosen, part / "default.xex", nullptr, error)) {
          fs::remove_all(part, ec);
          return false;
        }
      }
    }
  } else if (source.kind == SourceKind::kPackage) {
    Package package;
    if (!package.Open(source.chosen, error) || !package.ReadAll(error)) {
      fs::remove_all(part, ec);
      return false;
    }
    std::vector<uint8_t> data;
    for (const auto& e : package.entries) {
      if (progress && progress->cancel) {
        if (error) *error = "cancelled";
        fs::remove_all(part, ec);
        return false;
      }
      const fs::path to = part / FromUtf8(e.path);
      if (e.directory) {
        fs::create_directories(to, ec);
        continue;
      }
      if (!package.Extract(e, &data, error) || !WriteFile(to, data, error)) {
        fs::remove_all(part, ec);
        return false;
      }
      if (progress) progress->done += e.size;
    }
  } else {
    if (error) *error = "nothing to install";
    return false;
  }

  // Swap the new copy in. An older install is moved aside first and removed afterwards.
  const fs::path old = install_dir / (std::string(kGameFolder) + ".old");
  fs::remove_all(old, ec);
  if (fs::exists(target, ec)) {
    fs::rename(target, old, ec);
    if (ec) {
      if (error) *error = "could not replace the old " + std::string(kGameFolder) + " folder (is the game running?)";
      fs::remove_all(part, ec);
      return false;
    }
  }
  fs::rename(part, target, ec);
  if (ec) {
    if (error) *error = "could not finish the install: " + ec.message();
    std::error_code ignore;
    if (fs::exists(old, ignore)) fs::rename(old, target, ignore);
    return false;
  }
  fs::remove_all(old, ec);
  return true;
}

InstalledGame CheckInstalledGame(const fs::path& install_dir) {
  InstalledGame g;
  std::error_code ec;
  const fs::path folder = install_dir / kGameFolder;
  std::vector<uint8_t> xex;
  if (!ReadFile(folder / "default.xex", &xex, nullptr)) {
    g.summary = "Not installed yet";
    return g;
  }
  const XexInfo info = ReadXexInfo(xex.data(), xex.size());
  if (!info.ok || info.title_id != kTitleId) {
    g.summary = "The " + std::string(kGameFolder) + " folder holds another game";
    return g;
  }
  g.present = true;
  const ManifestFile* known = ManifestXex();
  g.known_version = known && Sha256Hex(xex.data(), xex.size()) == known->sha256;
  g.complete = true;
  if (g.known_version) {
    for (const ManifestFile& f : GameManifest()) {
      if (!fs::is_regular_file(folder / FromUtf8(f.path), ec)) {
        g.complete = false;
        break;
      }
    }
  }
  g.summary = !g.complete ? "Installed, but files are missing: install again"
              : g.known_version ? "Installed"
                                : "Installed (a version the port was not tested with)";
  return g;
}

// ---------------------------------------------------------------------------------------
DlcInfo CheckDlc(const fs::path& path) {
  DlcInfo d;
  d.path = path;
  d.file_name = PathText(path.filename());
  std::error_code ec;
  d.size = fs::file_size(path, ec);
  Package package;
  std::string error;
  if (!package.Open(path, &error)) {
    d.error = "Not a DLC package: " + error + ".";
    return d;
  }
  d.title = package.display_name.empty() ? d.file_name : package.display_name;
  if (package.title_id != kTitleId && package.title_id != kTestTitleId) {
    d.error = "This package is for another game (title " + Hex32(package.title_id) + ").";
    return d;
  }
  if (package.content_type == kContentArcade) {
    d.error = "This is the game itself; pick it under Game files.";
    return d;
  }
  if (package.content_type != kContentMarketplace) {
    d.error = "This package is not DLC (content type " + Hex32(package.content_type) + ").";
    return d;
  }
  d.ok = true;
  d.prototype = package.title_id == kTestTitleId || Lower(d.title).find("test") != std::string::npos;
  d.readable = package.ReadAll(&error);
  if (!d.readable) d.error = "Its contents could not be read (" + error + "); the game may skip it.";
  return d;
}

std::vector<fs::path> ExpandDlcPaths(const std::vector<fs::path>& picked) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const fs::path& p : picked) {
    if (fs::is_directory(p, ec)) {
      for (const auto& entry : fs::recursive_directory_iterator(p, ec)) {
        if (!entry.is_regular_file(ec) || entry.file_size(ec) < 0x1000) continue;
        std::vector<uint8_t> head;
        if (!ReadFile(entry.path(), &head, nullptr, 4) || head.size() < 4) continue;
        const std::string magic(head.begin(), head.end());
        if (magic == "LIVE" || magic == "PIRS" || magic == "CON ") out.push_back(entry.path());
      }
    } else {
      out.push_back(p);
    }
  }
  return out;
}

bool InstallDlc(const DlcInfo& dlc, const fs::path& install_dir, std::string* error) {
  std::error_code ec;
  const fs::path folder = install_dir / kDlcFolder;
  fs::create_directories(folder, ec);
  std::string name = dlc.file_name;
  if (name.size() > 42) name.resize(42);  // the game's content names are at most 42 characters
  const fs::path to = folder / FromUtf8(name);
  if (fs::exists(to, ec) && fs::equivalent(dlc.path, to, ec)) return true;
  if (fs::exists(to, ec) && fs::file_size(to, ec) == dlc.size) return true;  // already added
  return CopyWithProgress(dlc.path, to, nullptr, error);
}

bool RemoveDlc(const DlcInfo& dlc, std::string* error) {
  std::error_code ec;
  if (!fs::remove(dlc.path, ec) || ec) {
    if (error) *error = "could not remove " + dlc.file_name + (ec ? ": " + ec.message() : std::string());
    return false;
  }
  return true;
}

std::vector<DlcInfo> ListInstalledDlc(const fs::path& install_dir) {
  std::vector<DlcInfo> out;
  std::error_code ec;
  const fs::path folder = install_dir / kDlcFolder;
  if (!fs::is_directory(folder, ec)) return out;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    DlcInfo d;
    d.path = entry.path();
    d.file_name = PathText(entry.path().filename());
    d.size = entry.file_size(ec);
    Package package;
    std::string error;
    if (package.Open(entry.path(), &error)) {
      d.title = package.display_name.empty() ? d.file_name : package.display_name;
      d.ok = (package.title_id == kTitleId || package.title_id == kTestTitleId) &&
             package.content_type == kContentMarketplace;
      d.prototype = package.title_id == kTestTitleId || Lower(d.title).find("test") != std::string::npos;
      d.readable = true;
      if (!d.ok) d.error = "Not a DLC package for this game: the game ignores it.";
    } else {
      d.title = d.file_name;
      d.error = "Not a DLC package: the game ignores it.";
    }
    out.push_back(d);
  }
  std::sort(out.begin(), out.end(), [](const DlcInfo& a, const DlcInfo& b) { return a.title < b.title; });
  return out;
}

// ---------------------------------------------------------------------------------------
namespace {
std::string Trim(std::string v) {
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.pop_back();
  size_t i = 0;
  while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
  return v.substr(i);
}
// The cvar a line sets, or "" for comments and blank lines.
std::string KeyOf(const std::string& line) {
  const std::string t = Trim(line);
  const size_t eq = t.find('=');
  if (t.empty() || t[0] == '#' || eq == std::string::npos) return "";
  return Trim(t.substr(0, eq));
}
}  // namespace

fs::path SettingsFile::DefaultPath() {
  fs::path documents;
#if defined(_WIN32)
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &path))) {
    documents = path;
    CoTaskMemFree(path);
  }
#else
  if (const char* home = std::getenv("HOME")) documents = fs::path(home) / "Documents";
#endif
  return documents / "boomboomrocket" / "settings.ini";
}

bool SettingsFile::Load(const fs::path& path) {
  lines_.clear();
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines_.push_back(line);
  }
  return true;
}

bool SettingsFile::Has(const std::string& cvar) const {
  for (const std::string& line : lines_) {
    if (KeyOf(line) == cvar) return true;
  }
  return false;
}

std::string SettingsFile::Get(const std::string& cvar, const std::string& fallback) const {
  std::string value = fallback;
  for (const std::string& line : lines_) {  // the last one wins, as in the game
    if (KeyOf(line) == cvar) value = Trim(line.substr(line.find('=') + 1));
  }
  return value;
}

void SettingsFile::Set(const std::string& cvar, const std::string& value) {
  bool done = false;
  for (auto it = lines_.begin(); it != lines_.end();) {
    if (KeyOf(*it) != cvar) {
      ++it;
      continue;
    }
    if (done) {
      it = lines_.erase(it);  // a duplicate would override the new value
      continue;
    }
    *it = cvar + " = " + value;
    done = true;
    ++it;
  }
  if (!done) {
    if (lines_.empty()) lines_.push_back("# Boom Boom Rocket Revolution settings (written by the launcher)");
    lines_.push_back(cvar + " = " + value);
  }
}

bool SettingsFile::Save(const fs::path& path, std::string* error) const {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path temp = path.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out) {
      if (error) *error = "could not write " + PathText(path);
      return false;
    }
    for (const std::string& line : lines_) out << line << "\n";
    if (!out) {
      if (error) *error = "could not write " + PathText(path);
      return false;
    }
  }
  fs::rename(temp, path, ec);
  if (ec) {
    fs::remove(temp, ec);
    if (error) *error = "could not write " + PathText(path);
    return false;
  }
  return true;
}

}  // namespace bbrr
