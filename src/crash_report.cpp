// Logs and crash reports for release builds.
//
// Logs go to <user folder>/boomboomrocket/logs (next to settings.ini and the profiles),
// one numbered file per start; only the newest kLogsKept are kept. --log_file still
// writes to a single file instead (handy while developing).
//
// A crash outside what the SDK already reports (it logs bad guest memory reads and calls to
// unknown functions itself) used to end the log with no explanation. Now an unhandled
// exception or an abort writes a minidump next to the logs and a "[crash]" line naming the
// exception and the module + offset it happened at. The symbols zip made by
// scripts\package.ps1 turns a dump back into source lines. Log lines are written to disk one
// by one, so nothing is flushed here (the log's lock may be held by the thread that crashed).

#include "crash_report.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

#ifndef BBRR_VERSION_STRING
#define BBRR_VERSION_STRING "unknown"
#endif

REXCVAR_DEFINE_BOOL(crash_test, false, "BBR", "Crash on purpose once the game is set up, to check that a crash dump is written");

namespace bbr {
namespace {
constexpr size_t kLogsKept = 20;   // newest logs kept, including the one about to start
constexpr size_t kDumpsKept = 5;   // newest crash dumps kept

std::wstring g_dump_folder;
std::atomic<bool> g_crashing{false};

// Files in the log folder whose name starts with `prefix` and ends with `ext`, oldest first.
std::vector<std::filesystem::path> Files(const std::filesystem::path& folder, const std::wstring& prefix,
                                         const std::wstring& ext) {
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::wstring name = entry.path().filename().wstring();
    if (name.size() <= prefix.size() + ext.size() || name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - ext.size(), ext.size(), ext) != 0)
      continue;
    found.push_back({entry.last_write_time(ec), entry.path()});
  }
  std::sort(found.begin(), found.end());
  std::vector<std::filesystem::path> out;
  for (auto& f : found) out.push_back(std::move(f.second));
  return out;
}

void KeepNewest(const std::filesystem::path& folder, const std::wstring& prefix, const std::wstring& ext,
                size_t keep) {
  auto files = Files(folder, prefix, ext);
  std::error_code ec;
  for (size_t i = 0; i + keep < files.size(); ++i) std::filesystem::remove(files[i], ec);
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string RegistryString(HKEY root, const wchar_t* key, const wchar_t* value) {
  wchar_t buffer[256];
  DWORD size = sizeof(buffer);
  if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return "?";
  std::wstring w(buffer);
  while (!w.empty() && std::iswspace(w.back())) w.pop_back();
  return Narrow(w);
}

// "boomboomrocket.exe+0x1A2B3C" for a code address.
std::string Where(const void* address) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &module) ||
      !module) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(uintptr_t(address)));
    return text;
  }
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring name(path, n);
  name = name.substr(name.find_last_of(L"\\/") + 1);
  char offset[32];
  std::snprintf(offset, sizeof(offset), "+0x%llX",
                static_cast<unsigned long long>(uintptr_t(address) - uintptr_t(module)));
  return Narrow(name) + offset;
}

struct DumpRequest {
  EXCEPTION_POINTERS* exception;
  DWORD thread_id;
  std::wstring path;
  bool ok;
};

DWORD WINAPI WriteDumpThread(void* param) {
  auto* request = static_cast<DumpRequest*>(param);
  HANDLE file = CreateFileW(request->path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 0;
  MINIDUMP_EXCEPTION_INFORMATION info{request->thread_id, request->exception, FALSE};
  const auto type = MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                  MiniDumpWithUnloadedModules);
  request->ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                  request->exception ? &info : nullptr, nullptr, nullptr) != FALSE;
  CloseHandle(file);
  return 0;
}

// Writes the dump from a fresh thread: the crashed thread's stack may be the problem.
std::wstring WriteDump(EXCEPTION_POINTERS* exception) {
  if (g_dump_folder.empty()) return {};
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[64];
  swprintf(name, 64, L"crash_%04u%02u%02u_%02u%02u%02u.dmp", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
           t.wSecond);
  DumpRequest request{exception, GetCurrentThreadId(), g_dump_folder + L"\\" + name, false};
  HANDLE thread = CreateThread(nullptr, 0, WriteDumpThread, &request, 0, nullptr);
  if (!thread) return {};
  WaitForSingleObject(thread, 60000);
  CloseHandle(thread);
  return request.ok ? request.path : std::wstring();
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
  if (g_crashing.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
  const std::wstring dump = WriteDump(exception);
  const EXCEPTION_RECORD* record = exception ? exception->ExceptionRecord : nullptr;
  REXLOG_CRITICAL("[crash] exception 0x{:08X} at {} on thread {}", record ? uint32_t(record->ExceptionCode) : 0u,
                  record ? Where(record->ExceptionAddress) : std::string("?"), GetCurrentThreadId());
  if (record && record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
    REXLOG_CRITICAL("[crash] {} of address 0x{:X}", record->ExceptionInformation[0] == 1 ? "write" : "read",
                    record->ExceptionInformation[1]);
  REXLOG_CRITICAL("[crash] {}", dump.empty() ? std::string("no crash dump could be written")
                                             : "crash dump: " + Narrow(dump));
  return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int) {
  if (g_crashing.exchange(true)) return;
  const std::wstring dump = WriteDump(nullptr);
  REXLOG_CRITICAL("[crash] the game stopped itself (abort) on thread {}; {}", GetCurrentThreadId(),
                  dump.empty() ? std::string("no crash dump could be written") : "crash dump: " + Narrow(dump));
}
}  // namespace

std::filesystem::path LogFolder() { return rex::filesystem::GetUserFolder() / "boomboomrocket" / "logs"; }

void PruneLogFolder() {
  const auto folder = LogFolder();
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  KeepNewest(folder, L"boomboomrocket_", L".log", kLogsKept - 1);  // the new log makes kLogsKept
  KeepNewest(folder, L"crash_", L".dmp", kDumpsKept);
}

void LogStartInfo() {
  wchar_t exe[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const std::filesystem::path exe_path(std::wstring(exe, n));
  std::string built = "?";
  WIN32_FILE_ATTRIBUTE_DATA attributes;
  if (GetFileAttributesExW(exe_path.c_str(), GetFileExInfoStandard, &attributes)) {
    FILETIME local;
    SYSTEMTIME t;
    if (FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &t)) {
      char text[32];
      std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
      built = text;
    }
  }
  REXLOG_INFO("[bbrr] Boom Boom Rocket Revolution {} (exe from {}), {}", BBRR_VERSION_STRING, built,
              Narrow(exe_path.wstring()));

  // Windows version straight from ntdll (GetVersionEx lies to programs without a manifest).
  RTL_OSVERSIONINFOW os{sizeof(os)};
  using RtlGetVersionFn = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
  if (auto get = reinterpret_cast<RtlGetVersionFn>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
    get(&os);
  }
  const std::string display_version = RegistryString(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
  MEMORYSTATUSEX memory{sizeof(memory)};
  GlobalMemoryStatusEx(&memory);
  SYSTEM_INFO system;
  GetNativeSystemInfo(&system);
  REXLOG_INFO("[bbrr] Windows {}.{} build {} ({}); CPU {} ({} logical cores); {:.1f} GB memory",
              os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, display_version,
              RegistryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                             L"ProcessorNameString"),
              system.dwNumberOfProcessors, double(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0));
}

void InstallCrashHandler() {
  g_dump_folder = LogFolder().wstring();
  SetUnhandledExceptionFilter(OnUnhandledException);
  std::signal(SIGABRT, OnAbort);
  REXLOG_INFO("[bbrr] logs and crash dumps: {}", Narrow(g_dump_folder));
}

void CrashTest() {
  if (!REXCVAR_GET(crash_test)) return;
  REXLOG_WARN("[crash] --crash_test: crashing on purpose");
  *static_cast<volatile int*>(nullptr) = 0;
}
}  // namespace bbr
