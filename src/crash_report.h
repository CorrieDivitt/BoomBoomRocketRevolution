#pragma once
#include <filesystem>

// Logs and crash reports for release builds (src/crash_report.cpp).
namespace bbr {
// <user folder>/boomboomrocket/logs: one numbered log per start, plus crash dumps.
std::filesystem::path LogFolder();
// Deletes old logs and dumps so the folder keeps only the newest ones. Call before the
// new log is opened.
void PruneLogFolder();
// First lines of every log: version, build time, Windows, CPU and memory.
void LogStartInfo();
// On a crash, writes crash_<date>_<time>.dmp to the log folder and says where in the log.
void InstallCrashHandler();
// --crash_test=true: crashes on purpose, to check the crash handler.
void CrashTest();
}  // namespace bbr
