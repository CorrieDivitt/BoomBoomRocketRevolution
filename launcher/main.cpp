// Boom Boom Rocket Revolution launcher (Windows).
//
// Setup: the player points it at their own copy of the game (default.xex in the unpacked
// folder, or the original package) and, if they have any, their DLC packages. Once these are
// checked it installs them next to itself ("Game Files" and "DLC", where the game looks).
// Game Settings: the in-game Video Settings page plus Menu Layout and Skip Intro, written to
// Documents\boomboomrocket\settings.ini, which the game reads at start-up.
// Play: starts boomboomrocket.exe from the same folder.
//
// Dear ImGui on Direct3D 11. The checking and installing is in launcher_core.cpp.

#include "launcher_core.h"
#include "settings_table.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
namespace fs = std::filesystem;
using namespace bbrr;
using bbr::settings_table::Setting;

// ---------------------------------------------------------------------------------------
// Text helpers
std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
  return out;
}
std::string PathText(const fs::path& p) { return Utf8(p.wstring()); }

fs::path ExeFolder() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (n < buffer.size()) {
      buffer.resize(n);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return fs::path(buffer).parent_path();
}

// ---------------------------------------------------------------------------------------
// File dialogs (the Windows Explorer one)
std::vector<fs::path> PickFiles(HWND owner, bool multiple, const std::vector<COMDLG_FILTERSPEC>& filters,
                                const wchar_t* title) {
  std::vector<fs::path> out;
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return out;
  DWORD options = 0;
  dialog->GetOptions(&options);
  options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
  if (multiple) options |= FOS_ALLOWMULTISELECT;
  dialog->SetOptions(options);
  dialog->SetTitle(title);
  if (!filters.empty()) dialog->SetFileTypes(UINT(filters.size()), filters.data());
  if (SUCCEEDED(dialog->Show(owner))) {
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(dialog->GetResults(&items))) {
      DWORD count = 0;
      items->GetCount(&count);
      for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            out.emplace_back(path);
            CoTaskMemFree(path);
          }
          item->Release();
        }
      }
      items->Release();
    }
  }
  dialog->Release();
  return out;
}

// ---------------------------------------------------------------------------------------
// Short explanations for the Game Settings tab (as in docs/USER_GUIDE.md).
const char* Describe(const std::string& key) {
  static const std::map<std::string, const char*> text = {
      {"aspect", "Match Window fills any window shape; 16:9 keeps the original picture with bars at the sides."},
      {"window", "Fullscreen, a window, or a borderless window (no title bar or frame)."},
      {"antialiasing", "Smooths jagged edges. Most visible at lower resolutions."},
      {"glow", "A soft glow around fireworks and other bright things."},
      {"citylight", "Buildings near a firework are lit in its colour. Off is the original look."},
      {"waterglow", "The shine on the water takes the colour of the fireworks and moves to where they burst."},
      {"particles", "How many sparks each firework throws, as a multiple of the original (1x)."},
      {"hdr", "For HDR displays with HDR switched on in Windows."},
      {"resolution", "The resolution the game draws at. Higher is sharper and slower."},
      {"layout", "Where the menus sit on wide screens: at the screen edges, or centred as on a 16:9 TV."},
      {"skip_intro", "Skips the logo screens when the game starts."},
  };
  const auto it = text.find(key);
  return it == text.end() ? "" : it->second;
}

// The settings the launcher offers: the Video Settings page, then Menu Layout and Skip Intro.
std::vector<const Setting*> LauncherSettings() {
  std::vector<std::string> keys;
  for (const auto& page : bbr::settings_table::Pages()) {
    if (page.name == "video") keys = page.rows;
  }
  keys.push_back("layout");
  keys.push_back("skip_intro");
  std::vector<const Setting*> out;
  for (const std::string& key : keys) {
    for (const Setting& s : bbr::settings_table::Settings()) {
      if (s.key == key) out.push_back(&s);
    }
  }
  return out;
}

// Which choice a stored value is (numbers compare by value, as in the game).
int ChoiceFor(const Setting& s, const std::string& value) {
  for (size_t i = 0; i < s.choices.size(); ++i) {
    if (s.choices[i].value == value) return int(i);
  }
  char* end = nullptr;
  const double number = std::strtod(value.c_str(), &end);
  if (end != value.c_str()) {
    int best = int(s.default_choice);
    double best_error = 1e18;
    for (size_t i = 0; i < s.choices.size(); ++i) {
      char* e2 = nullptr;
      const double v = std::strtod(s.choices[i].value.c_str(), &e2);
      if (e2 == s.choices[i].value.c_str()) continue;
      if (std::fabs(v - number) < best_error) {
        best_error = std::fabs(v - number);
        best = int(i);
      }
    }
    return best;
  }
  return int(s.default_choice);
}

// ---------------------------------------------------------------------------------------
// Colours
const ImVec4 kAccent(1.00f, 0.42f, 0.16f, 1.0f);   // rocket orange
const ImVec4 kAccent2(0.96f, 0.26f, 0.55f, 1.0f);  // firework pink
const ImVec4 kGood(0.42f, 0.86f, 0.46f, 1.0f);
const ImVec4 kWarn(1.00f, 0.78f, 0.30f, 1.0f);
const ImVec4 kBad(1.00f, 0.38f, 0.38f, 1.0f);
const ImVec4 kDim(0.62f, 0.64f, 0.72f, 1.0f);

void ApplyStyle(float scale) {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  style.WindowRounding = 0.0f;
  style.FrameRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.ChildRounding = 8.0f;
  style.PopupRounding = 6.0f;
  style.FramePadding = ImVec2(12, 7);
  style.ItemSpacing = ImVec2(10, 9);
  style.WindowPadding = ImVec2(22, 18);
  style.ScaleAllSizes(scale);
  ImVec4* c = style.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.06f, 0.10f, 1.0f);
  c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.095f, 0.15f, 1.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.105f, 0.16f, 1.0f);
  c[ImGuiCol_Border] = ImVec4(0.20f, 0.21f, 0.30f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.15f, 0.22f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.20f, 0.29f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.24f, 0.34f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.18f, 0.19f, 0.28f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.26f, 0.37f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.31f, 0.44f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.20f, 0.21f, 0.31f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.27f, 0.39f, 1.0f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.31f, 0.44f, 1.0f);
  c[ImGuiCol_Tab] = ImVec4(0.11f, 0.12f, 0.18f, 1.0f);
  c[ImGuiCol_TabHovered] = ImVec4(0.96f, 0.26f, 0.55f, 0.55f);
  c[ImGuiCol_TabSelected] = ImVec4(0.96f, 0.26f, 0.55f, 0.85f);
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_Separator] = ImVec4(0.20f, 0.21f, 0.30f, 1.0f);
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.13f, 0.20f, 1.0f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
}

// A big coloured button; returns true when pressed.
bool BigButton(const char* label, const ImVec4& colour, bool enabled, ImVec2 size) {
  if (!enabled) ImGui::BeginDisabled();
  ImGui::PushStyleColor(ImGuiCol_Button, colour);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.0f, colour.x * 1.12f), std::min(1.0f, colour.y * 1.12f),
                                                       std::min(1.0f, colour.z * 1.12f), 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colour.x * 0.85f, colour.y * 0.85f, colour.z * 0.85f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  if (!enabled) ImGui::EndDisabled();
  return pressed && enabled;
}

void StatusLine(const ImVec4& colour, const char* mark, const std::string& text) {
  ImGui::TextColored(colour, "%s", mark);
  ImGui::SameLine();
  ImGui::TextWrapped("%s", text.c_str());
}

// ---------------------------------------------------------------------------------------
struct App {
  HWND hwnd = nullptr;
  fs::path install_dir;
  ImFont* body = nullptr;
  ImFont* bold = nullptr;
  float base_size = 18.0f;

  // What is installed now.
  InstalledGame installed;
  std::vector<DlcInfo> installed_dlc;

  // The player's picks, once checked.
  std::optional<GameSource> game;
  std::vector<DlcInfo> new_dlc;

  // The one background job (checking or installing) and its progress.
  std::thread job;
  std::atomic<bool> busy{false};
  Progress progress;
  std::mutex mutex;  // guards the results below
  std::optional<GameSource> checked;
  std::optional<std::vector<DlcInfo>> checked_dlc;
  std::optional<std::string> install_result;  // "" = success
  std::string message;
  ImVec4 message_colour = kGood;

  // Settings
  std::vector<const Setting*> settings;
  SettingsFile settings_file;
  fs::path settings_path;
  std::map<std::string, int> chosen;
  std::string settings_note;
  bool close_on_play = true;

  // Player 1's gamertag (profiles.ini)
  ProfilesFile profiles;
  fs::path profiles_path;
  char gamertag[ProfilesFile::kNameMax + 1] = {};
  std::string gamertag_note;

  void Refresh() {
    installed = CheckInstalledGame(install_dir);
    installed_dlc = ListInstalledDlc(install_dir);
  }

  void LoadSettings() {
    settings = LauncherSettings();
    settings_path = SettingsFile::DefaultPath();
    settings_file.Load(settings_path);
    for (const Setting* s : settings) {
      chosen[s->cvar] = ChoiceFor(*s, settings_file.Get(s->cvar, s->choices[s->default_choice].value));
      // A settings file from before Window Mode had three choices only has "fullscreen".
      if (s->cvar == "window_mode" && !settings_file.Has("window_mode") && settings_file.Has("fullscreen")) {
        chosen[s->cvar] = ChoiceFor(*s, settings_file.Get("fullscreen", "true") == "false" ? "windowed" : "fullscreen");
      }
    }
  }

  void LoadGamertag() {
    profiles_path = ProfilesFile::DefaultPath();
    profiles.Load(profiles_path);
    const std::string name = profiles.Player1Name();
    std::snprintf(gamertag, sizeof(gamertag), "%s", name.c_str());
  }

  void SaveGamertag() {
    const std::string name = ProfilesFile::Clean(gamertag);
    if (name.empty()) {  // nothing usable typed: put the current name back
      std::snprintf(gamertag, sizeof(gamertag), "%s", profiles.Player1Name().c_str());
      return;
    }
    std::snprintf(gamertag, sizeof(gamertag), "%s", name.c_str());
    if (name == profiles.Player1Name()) return;
    profiles.Load(profiles_path);  // the game may have changed it since
    profiles.SetPlayer1Name(name);
    std::string error;
    gamertag_note = profiles.Save(profiles_path, &error) ? "Saved." : "Could not save: " + error;
  }

  void DrawGamertag() {
    ImGui::PushFont(bold, 0.0f);
    ImGui::TextColored(kAccent, "Player");
    ImGui::PopFont();
    if (!ImGui::BeginTable("player", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) return;
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, base_size * 13.0f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Gamertag");
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("Player 1's name in the game and on the leaderboards (up to 15 characters). More players "
                       "and profiles: BBRR Settings > Profiles in the game.");
    ImGui::PopStyleColor();
    if (!gamertag_note.empty()) ImGui::TextColored(kDim, "%s", gamertag_note.c_str());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##gamertag", gamertag, sizeof(gamertag));
    if (ImGui::IsItemDeactivatedAfterEdit()) SaveGamertag();
    ImGui::EndTable();
    ImGui::Dummy(ImVec2(0, base_size * 0.2f));
  }

  void SaveSetting(const Setting& s) {
    settings_file.Set(s.cvar, s.choices[size_t(chosen[s.cvar])].value);
    std::string error;
    settings_note = settings_file.Save(settings_path, &error) ? "Saved. The game uses these the next time it starts."
                                                               : "Could not save: " + error;
  }

  void StartJob(std::function<void()> work) {
    if (busy) return;
    if (job.joinable()) job.join();
    progress.done = 0;
    progress.total = 0;
    progress.cancel = false;
    progress.SetStage("");
    busy = true;
    job = std::thread([this, work] {
      work();
      busy = false;
      PostMessageW(hwnd, WM_NULL, 0, 0);  // wake the window
    });
  }

  void PickGame() {
    const std::vector<COMDLG_FILTERSPEC> filters = {
        // Packages have no file extension, so the first filter shows every file.
        {L"default.xex or the game's package (all files)", L"*.*"},
        {L"default.xex", L"*.xex"},
    };
    const auto picked = PickFiles(hwnd, false, filters, L"Choose your copy of Boom Boom Rocket");
    if (!picked.empty()) CheckGame(picked[0]);
  }

  void CheckGame(const fs::path& path) {
    game.reset();
    message.clear();
    StartJob([this, path] {
      GameSource s = CheckGameSource(path, &progress);
      std::lock_guard<std::mutex> lock(mutex);
      checked = std::move(s);
    });
  }

  void PickDlc() {
    const std::vector<COMDLG_FILTERSPEC> filters = {
        // Packages have no file extension, so the filter shows every file.
        {L"DLC packages (all files)", L"*.*"},
    };
    const auto picked = PickFiles(hwnd, true, filters, L"Choose your Boom Boom Rocket DLC packages");
    if (!picked.empty()) CheckDlcFiles(picked);
  }

  void CheckDlcFiles(const std::vector<fs::path>& picked) {
    message.clear();
    StartJob([this, picked] {
      progress.SetStage("Checking the DLC");
      std::vector<DlcInfo> out;
      for (const fs::path& p : ExpandDlcPaths(picked)) out.push_back(CheckDlc(p));
      std::lock_guard<std::mutex> lock(mutex);
      checked_dlc = std::move(out);
    });
  }

  // Files dropped on the window: a game program or package goes to Game files, DLC to DLC.
  void Dropped(const std::vector<fs::path>& paths) {
    if (busy) return;
    std::vector<fs::path> dlc;
    std::optional<fs::path> game_path;
    for (const fs::path& p : ExpandDlcPaths(paths)) {
      Package package;
      std::string error;
      if (package.Open(p, &error) && package.content_type == kContentArcade) {
        game_path = p;
      } else {
        dlc.push_back(p);
      }
    }
    for (const fs::path& p : paths) {
      std::error_code ec;
      const fs::path xex = fs::is_directory(p, ec) ? p / "default.xex" : p;
      FILE* f = _wfopen(xex.c_str(), L"rb");
      if (!f) continue;
      char magic[4] = {};
      const size_t n = fread(magic, 1, 4, f);
      fclose(f);
      if (n == 4 && std::string(magic, 4) == "XEX2") {
        game_path = xex;
        dlc.erase(std::remove(dlc.begin(), dlc.end(), p), dlc.end());
      }
    }
    if (game_path) {
      CheckGame(*game_path);
    } else if (!dlc.empty()) {
      CheckDlcFiles(dlc);
    }
  }

  bool ReadyToInstall() const {
    const bool game_ok = game && game->ok;
    bool dlc_ok = false;
    for (const DlcInfo& d : new_dlc) dlc_ok |= d.ok;
    return !busy && (game_ok || (dlc_ok && installed.present));
  }

  void Install() {
    message.clear();
    std::optional<GameSource> source = game;
    std::vector<DlcInfo> dlc = new_dlc;
    StartJob([this, source, dlc] {
      std::string error;
      bool ok = true;
      if (source && source->ok) ok = InstallGame(*source, install_dir, &progress, &error);
      if (ok) {
        progress.SetStage("Installing the DLC");
        for (const DlcInfo& d : dlc) {
          if (!d.ok) continue;
          if (!InstallDlc(d, install_dir, &error)) {
            ok = false;
            break;
          }
        }
      }
      std::lock_guard<std::mutex> lock(mutex);
      install_result = ok ? std::string() : (error.empty() ? std::string("unknown error") : error);
    });
  }

  void Play() {
    const fs::path exe = install_dir / kGameExe;
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
      message = "The game program (" + std::string(kGameExe) + ") is not in the launcher's folder. "
                "Download the release again and keep its files together.";
      message_colour = kBad;
      return;
    }
    // Arguments given to the launcher are passed on to the game.
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
      if (std::wstring(argv[i]) == L"--play") continue;
      command += L" \"" + std::wstring(argv[i]) + L"\"";
    }
    if (argv) LocalFree(argv);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, install_dir.c_str(), &startup,
                        &process)) {
      message = "Could not start the game (Windows error " + std::to_string(GetLastError()) + ").";
      message_colour = kBad;
      return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (close_on_play) PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }

  // Results of the background job, taken on the window's thread.
  void Collect() {
    std::lock_guard<std::mutex> lock(mutex);
    if (checked) {
      game = std::move(*checked);
      checked.reset();
    }
    if (checked_dlc) {
      for (DlcInfo& d : *checked_dlc) {
        bool seen = false;
        for (const DlcInfo& n : new_dlc) seen |= (n.path == d.path);
        for (const DlcInfo& i : installed_dlc) seen |= (i.file_name == d.file_name && i.size == d.size);
        if (!seen) new_dlc.push_back(std::move(d));
      }
      checked_dlc.reset();
    }
    if (install_result) {
      if (install_result->empty()) {
        message = "Installed. Press Play to start the game.";
        message_colour = kGood;
        game.reset();
        new_dlc.clear();
      } else {
        message = "The install did not finish: " + *install_result + ".";
        message_colour = kBad;
      }
      install_result.reset();
      Refresh();
    }
  }

  // -------------------------------------------------------------------------------------
  void Heading(const char* text) {
    ImGui::PushFont(bold, base_size * 1.15f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
  }

  void DrawGameFiles() {
    Heading("Game files");
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped(
        "Boom Boom Rocket is not included. Choose your own copy: the default.xex in the unpacked game folder, or the "
        "game's package file as it came from your Xbox 360.");
    ImGui::PopStyleColor();
    if (!busy) {
      if (ImGui::Button(game ? "Choose a different copy..." : "Browse for the game...")) PickGame();
    } else {
      ImGui::BeginDisabled();
      ImGui::Button("Browse for the game...");
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (game) {
      const ImVec4 colour = !game->ok ? kBad : game->warnings.empty() ? kGood : kWarn;
      StatusLine(colour, game->ok ? "OK" : "X", game->summary);
      ImGui::TextColored(kDim, "%s", PathText(game->chosen).c_str());
      for (const std::string& e : game->errors) StatusLine(kBad, "-", e);
      for (const std::string& w : game->warnings) StatusLine(kWarn, "-", w);
    } else if (installed.present) {
      StatusLine(installed.complete ? kGood : kWarn, installed.complete ? "OK" : "!", installed.summary);
    } else {
      StatusLine(kDim, "-", "Not chosen yet");
    }
  }

  void DrawDlc() {
    Heading("DLC (optional)");
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped(
        "Add your DLC packages, the files as they came from the console. Unfinished prototype packs are welcome too: "
        "the game repairs what it can. You can also drop files or folders on this window.");
    ImGui::PopStyleColor();
    const bool disabled = busy;  // (picking starts a job, which sets busy)
    if (disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Add DLC...")) PickDlc();
    if (disabled) ImGui::EndDisabled();

    if (installed_dlc.empty() && new_dlc.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(kDim, "No DLC added");
      return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("dlc", 3, flags)) return;
    ImGui::TableSetupColumn("Pack", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, base_size * 6.0f);
    ImGui::TableHeadersRow();
    std::optional<size_t> remove_new, remove_installed;
    auto row = [&](const DlcInfo& d, bool is_installed, size_t index) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(d.title.c_str());
      const std::string name =
          d.file_name.size() > 20 ? d.file_name.substr(0, 8) + "..." + d.file_name.substr(d.file_name.size() - 4) : d.file_name;
      ImGui::TextColored(kDim, "%s, %s", name.c_str(), FormatBytes(d.size).c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", PathText(d.path).c_str());
      ImGui::TableNextColumn();
      if (!d.ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", d.error.c_str());
        ImGui::PopStyleColor();
      } else {
        const std::string state = std::string(is_installed ? "Installed" : "Ready to install") +
                                  (d.prototype ? " - prototype pack" : "");
        ImGui::TextColored(is_installed ? kGood : kAccent, "%s", state.c_str());
        if (!d.error.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
          ImGui::TextWrapped("%s", d.error.c_str());
          ImGui::PopStyleColor();
        }
      }
      ImGui::TableNextColumn();
      ImGui::PushID(int(index) + (is_installed ? 10000 : 0));
      if (disabled) ImGui::BeginDisabled();
      if (ImGui::SmallButton(is_installed ? "Remove" : "Don't add")) {
        if (is_installed) remove_installed = index;
        else remove_new = index;
      }
      if (disabled) ImGui::EndDisabled();
      ImGui::PopID();
    };
    for (size_t i = 0; i < installed_dlc.size(); ++i) row(installed_dlc[i], true, i);
    for (size_t i = 0; i < new_dlc.size(); ++i) row(new_dlc[i], false, i);
    ImGui::EndTable();
    if (remove_new) new_dlc.erase(new_dlc.begin() + std::ptrdiff_t(*remove_new));
    if (remove_installed) {
      std::string error;
      if (!RemoveDlc(installed_dlc[*remove_installed], &error)) {
        message = "Could not remove the pack: " + error + ".";
        message_colour = kBad;
      }
      Refresh();
    }
  }

  void DrawInstall() {
    Heading("Install");
    ImGui::TextColored(kDim, "Installs into the launcher's folder: %s", PathText(install_dir).c_str());
    if (busy) {
      const uint64_t total = progress.total, done = progress.done;
      const float fraction = total ? float(double(done) / double(total)) : 0.0f;
      ImGui::TextUnformatted(progress.Stage().empty() ? "Working..." : (progress.Stage() + "...").c_str());
      ImGui::ProgressBar(total ? std::min(1.0f, fraction) : -1.0f * float(ImGui::GetTime()), ImVec2(-1, 0),
                         total ? (FormatBytes(done) + " of " + FormatBytes(total)).c_str() : "");
      if (ImGui::Button("Cancel")) progress.cancel = true;
      return;
    }
    const bool ready = ReadyToInstall();
    if (ready) {
      const bool game_ok = game && game->ok;
      size_t packs = 0;
      for (const DlcInfo& d : new_dlc) packs += d.ok ? 1 : 0;
      std::string what = game_ok ? "the game" : "";
      if (packs) what += std::string(game_ok ? " and " : "") + std::to_string(packs) + " DLC pack" + (packs > 1 ? "s" : "");
      StatusLine(kGood, ">", "Ready to install " + what + ".");
      if (game_ok && installed.present) {
        ImGui::TextColored(kDim, "The game files already in the folder will be replaced. Saves and settings are kept.");
      }
    } else if (!installed.present) {
      StatusLine(kDim, "-", game && !game->ok ? "Fix the problem above, or choose another copy."
                                              : "Choose your game files to continue.");
    }
    if (BigButton("Install", kAccent, ready, ImVec2(base_size * 9.0f, 0))) Install();
    if (!message.empty()) StatusLine(message_colour, "", message);
  }

  void DrawSetup() {
    const float gap = ImGui::GetStyle().ItemSpacing.y * 1.5f;
    DrawGameFiles();
    ImGui::Dummy(ImVec2(0, gap));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, gap));
    DrawDlc();
    ImGui::Dummy(ImVec2(0, gap));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, gap));
    DrawInstall();
  }

  void DrawSettings() {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped(
        "These settings can be set now or changed in game. Check out the BBRR menu in game for new gameplay "
        "options too.");
    ImGui::PopStyleColor();
    DrawGamertag();
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("settings", 2, flags)) {
      ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 3.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, base_size * 13.0f);
      bool menu_started = false;
      for (const Setting* s : settings) {
        if (s->page != "video" && !menu_started) {
          menu_started = true;
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Dummy(ImVec2(0, base_size * 0.2f));
          ImGui::PushFont(bold, 0.0f);
          ImGui::TextColored(kAccent, "Menus");
          ImGui::PopFont();
        } else if (s == settings.front()) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::PushFont(bold, 0.0f);
          ImGui::TextColored(kAccent, "Video");
          ImGui::PopFont();
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s->title.c_str());
        if (s->restart) {
          ImGui::SameLine();
          ImGui::TextColored(kDim, "(restart required)");
        }
        const char* text = Describe(s->key);
        if (*text) {
          ImGui::PushStyleColor(ImGuiCol_Text, kDim);
          ImGui::TextWrapped("%s", text);
          ImGui::PopStyleColor();
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        int& current = chosen[s->cvar];
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID(s->cvar.c_str());
        const std::string preview = s->choices[size_t(current)].label +
                                    (size_t(current) == s->default_choice ? "  (default)" : "");
        if (ImGui::BeginCombo("##v", preview.c_str())) {
          for (size_t i = 0; i < s->choices.size(); ++i) {
            const std::string label = s->choices[i].label + (i == s->default_choice ? "  (default)" : "");
            if (ImGui::Selectable(label.c_str(), int(i) == current)) {
              current = int(i);
              SaveSetting(*s);
            }
            if (int(i) == current) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::Button("Restore defaults")) {
      for (const Setting* s : settings) {
        chosen[s->cvar] = int(s->default_choice);
        settings_file.Set(s->cvar, s->choices[s->default_choice].value);
      }
      std::string error;
      settings_note = settings_file.Save(settings_path, &error) ? "Defaults restored." : "Could not save: " + error;
    }
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s", settings_note.empty() ? PathText(settings_path).c_str() : settings_note.c_str());
  }

  void Draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Title
    ImGui::PushFont(bold, base_size * 1.9f);
    ImGui::TextColored(kAccent, "Boom Boom Rocket");
    ImGui::SameLine(0, base_size * 0.4f);
    ImGui::TextColored(kAccent2, "Revolution");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + base_size * 0.75f);
    ImGui::TextColored(kDim, "launcher %s", LAUNCHER_VERSION);

    // Footer height: the Play row.
    const float footer = ImGui::GetFrameHeight() * 1.6f + ImGui::GetStyle().ItemSpacing.y * 3.0f;
    ImGui::BeginChild("body", ImVec2(0, -footer), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
    if (ImGui::BeginTabBar("tabs")) {
      if (ImGui::BeginTabItem("  Setup  ")) {
        ImGui::Dummy(ImVec2(0, 2));
        DrawSetup();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("  Game Settings  ")) {
        ImGui::Dummy(ImVec2(0, 2));
        DrawSettings();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // Footer: Play
    ImGui::Separator();
    const bool can_play = installed.present && !busy;
    ImGui::PushFont(bold, base_size * 1.2f);
    if (BigButton("Play", kAccent2, can_play, ImVec2(base_size * 10.0f, ImGui::GetFrameHeight() * 1.3f))) Play();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Checkbox("Close the launcher when the game starts", &close_on_play);
    if (!can_play) {
      ImGui::TextColored(kDim, "%s", busy ? "Please wait..." : "Install the game first (Setup tab).");
    } else if (ImGui::SmallButton("Open the game folder")) {
      ShellExecuteW(nullptr, L"open", install_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::EndGroup();
    ImGui::End();
  }
};

App* g_app = nullptr;

// ---------------------------------------------------------------------------------------
// Direct3D 11
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;

void CreateTarget() {
  ID3D11Texture2D* back = nullptr;
  g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
  if (back) {
    g_device->CreateRenderTargetView(back, nullptr, &g_target);
    back->Release();
  }
}
void ReleaseTarget() {
  if (g_target) {
    g_target->Release();
    g_target = nullptr;
  }
}

bool CreateDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &got, &g_context);
  if (hr == DXGI_ERROR_UNSUPPORTED) {  // no GPU driver: software
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                       &g_swap, &g_device, &got, &g_context);
  }
  if (FAILED(hr)) return false;
  CreateTarget();
  return true;
}

void ReleaseDevice() {
  ReleaseTarget();
  if (g_swap) g_swap->Release();
  if (g_context) g_context->Release();
  if (g_device) g_device->Release();
  g_swap = nullptr;
  g_context = nullptr;
  g_device = nullptr;
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;
  switch (msg) {
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) {
        g_resize_w = LOWORD(lparam);
        g_resize_h = HIWORD(lparam);
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const float scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
      info->ptMinTrackSize.x = LONG(760 * scale);
      info->ptMinTrackSize.y = LONG(560 * scale);
      return 0;
    }
    case WM_DROPFILES: {
      HDROP drop = reinterpret_cast<HDROP>(wparam);
      std::vector<fs::path> paths;
      const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < count; ++i) {
        const UINT len = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(len + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), len + 1);
        path.resize(len);
        paths.emplace_back(path);
      }
      DragFinish(drop);
      if (g_app) g_app->Dropped(paths);
      return 0;
    }
    case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ImGui_ImplWin32_EnableDpiAwareness();
  const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

  App app;
  g_app = &app;
  app.install_dir = ExeFolder();
  app.Refresh();
  app.LoadSettings();
  app.LoadGamertag();

  // "--play" with the game installed: start it straight away (for shortcuts and launchers).
  {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool play = false;
    for (int i = 1; argv && i < argc; ++i) play |= std::wstring(argv[i]) == L"--play";
    if (argv) LocalFree(argv);
    if (play && app.installed.present) {
      app.Play();
      return 0;
    }
  }

  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
                 L"BBRRLauncher", nullptr};
  wc.hIcon = LoadIconW(instance, L"IDI_ICON1");
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);
  const int width = int(980 * scale), height = int(760 * scale);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int x = work.left + std::max(0, int((work.right - work.left - width) / 2));
  const int y = work.top + std::max(0, int((work.bottom - work.top - height) / 2));
  HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Boom Boom Rocket Revolution", WS_OVERLAPPEDWINDOW, x,
                              y, width, height, nullptr, nullptr, instance, nullptr);
  app.hwnd = hwnd;
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

  if (!CreateDevice(hwnd)) {
    ReleaseDevice();
    MessageBoxW(nullptr, L"Could not start Direct3D 11.", L"Boom Boom Rocket Revolution", MB_ICONERROR);
    return 1;
  }
  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ApplyStyle(scale);
  ImGuiStyle& style = ImGui::GetStyle();
  style.FontScaleDpi = scale;
  style.FontSizeBase = app.base_size;

  wchar_t windows[MAX_PATH] = {};
  GetWindowsDirectoryW(windows, MAX_PATH);
  const fs::path fonts = fs::path(windows) / "Fonts";
  std::error_code ec;
  if (fs::exists(fonts / "segoeui.ttf", ec)) {
    app.body = io.Fonts->AddFontFromFileTTF(PathText(fonts / "segoeui.ttf").c_str(), app.base_size);
  }
  if (!app.body) app.body = io.Fonts->AddFontDefault();
  if (fs::exists(fonts / "segoeuib.ttf", ec)) {
    app.bold = io.Fonts->AddFontFromFileTTF(PathText(fonts / "segoeuib.ttf").c_str(), app.base_size);
  }
  if (!app.bold) app.bold = app.body;

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);

  bool done = false;
  while (!done) {
    // Sleep until something happens, unless a job is running (its progress bar moves).
    if (!app.busy) MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;
    if (g_resize_w && g_resize_h) {
      ReleaseTarget();
      g_swap->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
      g_resize_w = g_resize_h = 0;
      CreateTarget();
    }
    app.Collect();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(app.body, 0.0f);
    app.Draw();
    ImGui::PopFont();
    ImGui::Render();
    const float clear[4] = {0.055f, 0.06f, 0.10f, 1.0f};
    g_context->OMSetRenderTargets(1, &g_target, nullptr);
    g_context->ClearRenderTargetView(g_target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
  }

  app.progress.cancel = true;
  if (app.job.joinable()) app.job.join();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  ReleaseDevice();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  CoUninitialize();
  return 0;
}
