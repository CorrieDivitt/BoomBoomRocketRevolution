// Checks launcher_core against real files (not part of the build; game data stays local).
//   g++ -std=c++20 -O2 -Ilauncher launcher/core_test.cpp launcher/launcher_core.cpp -o core_test
//   ./core_test "<game folder or default.xex or package>" [<dlc package> ...]
#include "launcher_core.h"

#include <cstdio>
#include <cstring>

using namespace bbrr;

static void Print(const GameSource& s) {
  std::printf("kind=%d ok=%d known=%d bytes=%llu summary=%s\n", int(s.kind), s.ok, s.known_version,
              static_cast<unsigned long long>(s.bytes), s.summary.c_str());
  for (const auto& w : s.warnings) std::printf("  warning: %s\n", w.c_str());
  for (const auto& e : s.errors) std::printf("  error: %s\n", e.c_str());
}

int main(int argc, char** argv) {
  const std::string abc = Sha256Hex("abc", 3);
  std::printf("sha256(abc) %s\n", abc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ? "ok" : abc.c_str());
  std::string big(1000003, 'x');
  Sha256 a;
  a.Update(big.data(), 7);
  a.Update(big.data() + 7, big.size() - 7);
  std::printf("sha256 split %s\n", a.HexDigest() == Sha256Hex(big.data(), big.size()) ? "ok" : "MISMATCH");
  std::printf("manifest %zu files\n", GameManifest().size());
  if (argc < 2) return 0;
  Progress p;
  GameSource s = CheckGameSource(argv[1], &p);
  Print(s);
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--install") == 0 && i + 1 < argc) {
      std::string err;
      const bool ok = InstallGame(s, argv[i + 1], &p, &err);
      std::printf("install -> %d %s\n", ok, err.c_str());
      InstalledGame g = CheckInstalledGame(argv[i + 1]);
      std::printf("installed: present=%d complete=%d known=%d %s\n", g.present, g.complete, g.known_version,
                  g.summary.c_str());
      ++i;
      continue;
    }
    if (std::strcmp(argv[i], "--extract") == 0 && i + 2 < argc) {
      Package pk;
      std::string err;
      if (!pk.Open(argv[i + 1], &err) || !pk.ReadAll(&err)) {
        std::printf("extract open failed: %s\n", err.c_str());
      } else {
        for (const auto& e : pk.entries) {
          std::vector<uint8_t> d;
          if (e.directory) continue;
          if (!pk.Extract(e, &d, &err)) std::printf("  %s FAILED %s\n", e.path.c_str(), err.c_str());
          else std::printf("  %s %u %s\n", e.path.c_str(), e.size, Sha256Hex(d.data(), d.size()).c_str());
        }
      }
      i += 2;
      continue;
    }
    DlcInfo d = CheckDlc(argv[i]);
    std::printf("dlc %s: ok=%d proto=%d readable=%d title=\"%s\" %s %s\n", d.file_name.c_str(), d.ok, d.prototype,
                d.readable, d.title.c_str(), FormatBytes(d.size).c_str(), d.error.c_str());
  }
  SettingsFile f;
  f.Set("city_lighting", "4");
  f.Set("skip_intro", "true");
  f.Set("city_lighting", "8");
  std::printf("settings get %s %s\n", f.Get("city_lighting", "?").c_str(), f.Get("nope", "dflt").c_str());
  return 0;
}
