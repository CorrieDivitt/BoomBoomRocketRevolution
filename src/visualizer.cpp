// Visualizer mode fed by whatever is playing on the PC.
//
// On the Xbox 360 the Visualizer reacted to music from the console's own player (the Guide's
// "Select music"). The game asks the system music player for its output with XMPCaptureOutput
// (game wrapper sub_8215EFA0, XMP message 0x7003D) when the Visualizer starts (sub_8207D250,
// state 2), passing a callback (sub_82076F68). The player then hands that callback the music,
// 256 stereo frames at a time: 512 big-endian floats, interleaved, -1..1. The callback measures
// the level (sub_820605F8), takes an FFT of one channel (sub_82060000) and finds the beats
// (sub_82076930) that launch the rockets.
//
// Here the music is whatever Windows is playing (a music app, a browser...): WASAPI loopback
// capture of the PC's output, leaving out this game's own sound where Windows allows it
// (process loopback, Windows 10 2004 and later), so the fireworks do not feed back into the
// beat finder. The captured audio waits in a queue and is handed to the callback on the game's
// own thread, once per game update, so the game sees it just as it would from the console.
// The game's music player is not involved.

#include "visualizer.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#endif

REXCVAR_DEFINE_BOOL(visualizer_capture, true, "BBR",
                    "Visualizer mode reacts to the music playing on the PC (any app)");
REXCVAR_DEFINE_BOOL(visualizer_include_game, false, "BBR",
                    "Visualizer: also listen to the game's own sound (only used when Windows cannot "
                    "leave it out)");

REXCVAR_DEFINE_DOUBLE(visualizer_sensitivity, 1.0, "BBR",
                      "Visualizer: how much it reacts to the music's beats (0.5 low, 1 normal, 2 high, 4 very "
                      "high). The music's level is evened out first, so the PC's volume does not matter");

REX_EXTERN(__imp__sub_8215EFA0);
REX_EXTERN(__imp__sub_8207D250);
REX_EXTERN(__imp__sub_8207D5F0);
REX_EXTERN(sub_82076F68);

namespace {

constexpr uint32_t kKnownCallback = 0x82076F68;
constexpr size_t kChunkFrames = 256;     // per callback: 256 stereo frames = 512 floats
constexpr size_t kMaxQueued = 48000;     // frames kept waiting (1 s); older ones are dropped
constexpr int kMaxChunksPerUpdate = 12;  // keeps a slow frame from flooding the game

std::atomic<uint32_t> g_callback{0};
std::atomic<bool> g_enabled{false};

std::mutex g_queue_mutex;
std::deque<float> g_queue;  // interleaved stereo, host floats
std::atomic<uint32_t> g_rate{48000};

std::atomic<bool> g_thread_run{false};
std::thread g_thread;
std::atomic<int> g_mode{0};  // 0 none, 1 PC without this game, 2 PC with this game
std::chrono::steady_clock::time_point g_last_report{};
uint64_t g_chunks_sent = 0;
float g_peak = 0.0f;
// Level matching: the loudness of the music (a peak that falls slowly, about half in 3 s), so
// quiet and loud sources both reach the game near full scale before the sensitivity applies.
float g_envelope = 0.0f;
float g_gain_seen = 1.0f;
constexpr float kTargetPeak = 0.8f;
constexpr float kEnvelopeFall = 0.9988f;  // per 256-frame chunk (5.3 ms at 48 kHz)
// Sensitivity. The game finds beats by how much louder a moment is than the music around it,
// so turning everything up or down changes little (tried first). Instead each chunk's
// loudness compared with the recent average is squeezed (fewer, only the strongest beats) or
// stretched (more beats): the chunk is scaled by (rms / average)^(exponent - 1), with
// exponent 0.35 / 1 / 1.6 / 2.2 for Low / Normal / High / Very High.
float g_average_rms = 0.0f;
constexpr float kAverageFall = 0.985f;  // about 0.35 s at 48 kHz

// The launcher's own limits, in the game state at 0x821A0578 (read in the update after the
// spectrum is smoothed, function after sub_82076930): while the music is above a level (+268)
// a timer (+264) runs, and a rocket goes up each time it passes an interval (+260). The
// sensitivity also shortens / lengthens that interval and lowers / raises that level. The
// game's values are kept and put back when the Visualizer ends. (Read from the code, not yet
// confirmed in play; the first values seen are logged.)
constexpr uint32_t kGameState = 0x821A0578;
constexpr uint32_t kIntervalOffset = 260, kLevelOffset = 268;
float g_orig_interval = 0.0f, g_orig_level = 0.0f;
bool g_have_orig = false;
std::atomic<bool> g_restore_limits{false};  // put the game's values back at the next update

float LoadBigEndianFloat(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreLimit(uint8_t* base, uint32_t offset, float f);

void ApplyLimits(uint8_t* base, double setting) {
  if (!g_have_orig) {
    g_orig_interval = LoadBigEndianFloat(base + kGameState + kIntervalOffset);
    g_orig_level = LoadBigEndianFloat(base + kGameState + kLevelOffset);
    g_have_orig = true;
    REXLOG_INFO("[visualizer] launcher interval {:.3f}, level {:.3f}", g_orig_interval, g_orig_level);
  }
  const float rate = setting <= 0.75 ? 0.5f : setting <= 1.5 ? 1.0f : setting <= 3.0 ? 1.6f : 2.5f;
  const float level = setting <= 0.75 ? 1.5f : setting <= 1.5 ? 1.0f : setting <= 3.0 ? 0.75f : 0.55f;
  if (std::isfinite(g_orig_interval) && g_orig_interval > 0.0f && g_orig_interval < 100.0f)
    StoreLimit(base, kIntervalOffset, g_orig_interval / rate);
  if (std::isfinite(g_orig_level) && g_orig_level > 0.0f && g_orig_level < 1e6f)
    StoreLimit(base, kLevelOffset, g_orig_level * level);
}

void RestoreLimits(uint8_t* base) {
  if (!g_have_orig || !base) return;
  StoreLimit(base, kIntervalOffset, g_orig_interval);
  StoreLimit(base, kLevelOffset, g_orig_level);
  g_have_orig = false;
}

void Push(const float* stereo, size_t frames) {
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  g_queue.insert(g_queue.end(), stereo, stereo + frames * 2);
  while (g_queue.size() > kMaxQueued * 2) g_queue.pop_front();
}

#if defined(_WIN32)
// Completion handler for ActivateAudioInterfaceAsync (process loopback).
class ActivateHandler : public IActivateAudioInterfaceCompletionHandler {
 public:
  HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HRESULT result = E_FAIL;
  IAudioClient* client = nullptr;
  ~ActivateHandler() { CloseHandle(done); }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override { return --refs_; }  // lives on the stack
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
      *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
      return S_OK;
    }
    if (iid == __uuidof(IAgileObject)) {  // called from another apartment
      *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
    HRESULT hr = E_FAIL;
    IUnknown* unknown = nullptr;
    if (SUCCEEDED(op->GetActivateResult(&hr, &unknown)) && SUCCEEDED(hr) && unknown) {
      hr = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client));
      unknown->Release();
    }
    result = hr;
    SetEvent(done);
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
};

// Everything playing on the PC except this process (Windows 10 2004+). Null if not available.
IAudioClient* OpenProcessLoopback() {
  using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*,
                                      IActivateAudioInterfaceAsyncOperation**);
  HMODULE mmdevapi = LoadLibraryW(L"Mmdevapi.dll");
  auto activate = mmdevapi ? reinterpret_cast<ActivateFn>(GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync"))
                           : nullptr;
  if (!activate) return nullptr;
  AUDIOCLIENT_ACTIVATION_PARAMS params = {};
  params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
  params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
  params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
  PROPVARIANT variant = {};
  variant.vt = VT_BLOB;
  variant.blob.cbSize = sizeof(params);
  variant.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
  ActivateHandler handler;
  IActivateAudioInterfaceAsyncOperation* op = nullptr;
  if (FAILED(activate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &variant, &handler, &op))) {
    return nullptr;
  }
  WaitForSingleObject(handler.done, 5000);
  if (op) op->Release();
  return SUCCEEDED(handler.result) ? handler.client : nullptr;
}

// The default output device, game included.
IAudioClient* OpenEndpointLoopback() {
  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioClient* client = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator))) &&
      SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
    device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
  }
  if (device) device->Release();
  if (enumerator) enumerator->Release();
  return client;
}

void CaptureThread() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IAudioClient* client = OpenProcessLoopback();
  bool excludes_game = client != nullptr;
  WAVEFORMATEX* mix = nullptr;
  WAVEFORMATEX pcm = {};
  if (client) {
    // Process loopback has no mix format of its own: ask for 16-bit stereo at 48 kHz.
    pcm.wFormatTag = WAVE_FORMAT_PCM;
    pcm.nChannels = 2;
    pcm.nSamplesPerSec = 48000;
    pcm.wBitsPerSample = 16;
    pcm.nBlockAlign = 4;
    pcm.nAvgBytesPerSec = 48000 * 4;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  200000, 0, &pcm, nullptr))) {
      client->Release();
      client = nullptr;
      excludes_game = false;
    }
  }
  if (!client && REXCVAR_GET(visualizer_include_game)) {
    client = OpenEndpointLoopback();
    if (client && SUCCEEDED(client->GetMixFormat(&mix)) &&
        FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  200000, 0, mix, nullptr))) {
      client->Release();
      client = nullptr;
    }
  }
  if (!client) {
    REXLOG_WARN("[visualizer] cannot listen to the PC's sound (Windows 10 2004 or later is needed to leave the "
                "game out; --visualizer_include_game listens to everything instead)");
    g_mode.store(0);
    CoUninitialize();
    return;
  }
  const WAVEFORMATEX& format = mix ? *mix : pcm;
  const bool is_float =
      format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
      (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
       reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  const uint32_t channels = format.nChannels;
  g_rate.store(format.nSamplesPerSec);
  HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  IAudioCaptureClient* capture = nullptr;
  client->SetEventHandle(ready);
  client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
  client->Start();
  g_mode.store(excludes_game ? 1 : 2);
  REXLOG_INFO("[visualizer] listening to the PC's sound ({}), {} Hz, {} channel(s)",
              excludes_game ? "without this game" : "game included", format.nSamplesPerSec, channels);
  std::vector<float> stereo;
  while (g_thread_run.load() && capture) {
    WaitForSingleObject(ready, 100);
    UINT32 packet = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
      stereo.assign(size_t(frames) * 2, 0.0f);
      if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
        for (UINT32 f = 0; f < frames; ++f) {
          for (uint32_t c = 0; c < 2; ++c) {
            const uint32_t from = std::min(c, channels - 1);
            float v;
            if (is_float) {
              v = reinterpret_cast<const float*>(data)[size_t(f) * channels + from];
            } else {
              v = reinterpret_cast<const int16_t*>(data)[size_t(f) * channels + from] / 32768.0f;
            }
            stereo[size_t(f) * 2 + c] = v;
          }
        }
      }
      capture->ReleaseBuffer(frames);
      Push(stereo.data(), frames);
    }
  }
  client->Stop();
  if (capture) capture->Release();
  client->Release();
  if (mix) CoTaskMemFree(mix);
  CloseHandle(ready);
  g_mode.store(0);
  CoUninitialize();
}
#endif

void StartCapture() {
  if (g_thread_run.exchange(true)) return;
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_queue.clear();
  }
#if defined(_WIN32)
  if (g_thread.joinable()) g_thread.join();
  g_thread = std::thread(CaptureThread);
#endif
}

void StopCapture() {
  g_restore_limits.store(true);
  if (!g_thread_run.exchange(false)) return;
  if (g_thread.joinable()) g_thread.join();
  REXLOG_INFO("[visualizer] stopped listening ({} chunks given to the game)", g_chunks_sent);
}

void StoreBigEndianFloat(uint8_t* p, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(p, &v, 4);
}

void StoreLimit(uint8_t* base, uint32_t offset, float f) { StoreBigEndianFloat(base + kGameState + offset, f); }


// Hands the queued audio to the game's capture callback, on the game's thread.
void Deliver(PPCContext& ctx, uint8_t* base) {
  if (g_restore_limits.exchange(false)) RestoreLimits(base);
  if (!g_enabled.load() || g_callback.load() != kKnownCallback) return;
  ApplyLimits(base, std::clamp(REXCVAR_GET(visualizer_sensitivity), 0.1, 8.0));
  const PPCContext saved = ctx;
  // A buffer for the 512 floats below the game's stack, and the call's own stack below that.
  const uint32_t buffer = (uint32_t(ctx.r1.u32) - 0x1000) & ~0xFu;
  ctx.r1.u64 = buffer - 0x2000;
  float chunk[kChunkFrames * 2];
  for (int n = 0; n < kMaxChunksPerUpdate; ++n) {
    {
      std::lock_guard<std::mutex> lock(g_queue_mutex);
      if (g_queue.size() < kChunkFrames * 2) break;
      std::copy(g_queue.begin(), g_queue.begin() + kChunkFrames * 2, chunk);
      g_queue.erase(g_queue.begin(), g_queue.begin() + kChunkFrames * 2);
    }
    float chunk_peak = 0.0f;
    for (size_t i = 0; i < kChunkFrames * 2; ++i) chunk_peak = std::max(chunk_peak, std::fabs(chunk[i]));
    g_peak = std::max(g_peak, chunk_peak);
    g_envelope = std::max(chunk_peak, g_envelope * kEnvelopeFall);
    // Silence (or nearly) is left alone rather than turned up into noise.
    const float level = g_envelope > 0.01f ? std::clamp(kTargetPeak / g_envelope, 0.25f, 16.0f) : 1.0f;
    const double setting = std::clamp(REXCVAR_GET(visualizer_sensitivity), 0.1, 8.0);
    const float exponent = setting <= 0.75 ? 0.35f : setting <= 1.5 ? 1.0f : setting <= 3.0 ? 1.6f : 2.2f;
    float sum = 0.0f;
    for (size_t i = 0; i < kChunkFrames * 2; ++i) sum += chunk[i] * chunk[i];
    const float rms = std::sqrt(sum / float(kChunkFrames * 2));
    g_average_rms = g_average_rms * kAverageFall + rms * (1.0f - kAverageFall);
    float shape = 1.0f;
    if (exponent != 1.0f && g_average_rms > 1e-4f && rms > 1e-5f) {
      shape = std::clamp(std::pow(rms / g_average_rms, exponent - 1.0f), 0.1f, 6.0f);
    }
    const float gain = level * shape;
    g_gain_seen = gain;
    for (size_t i = 0; i < kChunkFrames * 2; ++i) {
      StoreBigEndianFloat(base + buffer + uint32_t(i) * 4, std::clamp(chunk[i] * gain, -1.0f, 1.0f));
    }
    ctx.r3.u64 = buffer;
    sub_82076F68(ctx, base);
    ++g_chunks_sent;
  }
  ctx = saved;
  const auto now = std::chrono::steady_clock::now();
  if (now - g_last_report > std::chrono::seconds(10)) {
    g_last_report = now;
    REXLOG_INFO("[visualizer] {} chunks of music given to the game so far; loudest sample {:.2f}, gain {:.2f} "
                "(sensitivity {:.2f})",
                g_chunks_sent, g_peak, g_gain_seen, REXCVAR_GET(visualizer_sensitivity));
    g_peak = 0.0f;
  }
}

}  // namespace

// XMPCaptureOutput(callback, context, enable, overlapped): the system music player would call
// `callback` with its output from now on. Answered here (the SDK has no music player).
REX_HOOK_RAW(sub_8215EFA0) {
  const uint32_t callback = ctx.r3.u32, enable = ctx.r5.u32;
  REXLOG_INFO("[visualizer] the game asks for the music player's output: callback {:08X}, {}", callback,
              enable ? "on" : "off");
  if (enable && callback && REXCVAR_GET(visualizer_capture)) {
    if (callback != kKnownCallback) {
      REXLOG_WARN("[visualizer] unexpected callback {:08X}; not feeding it", callback);
    }
    g_callback.store(callback);
    g_enabled.store(true);
    StartCapture();
  } else {
    g_enabled.store(false);
    StopCapture();
  }
  ctx.r3.u64 = 0;  // ERROR_SUCCESS
}

// The game changes mode (2 = Visualizer, see sub_8207D250): stop listening when it leaves.
REX_HOOK_RAW(sub_8207D250) {
  const uint32_t state = ctx.r4.u32;
  if (state != 2 && g_enabled.load()) {
    g_enabled.store(false);
    StopCapture();
  }
  __imp__sub_8207D250(ctx, base);
}

// The game's main update: hand over the music captured since the last one.
REX_HOOK_RAW(sub_8207D5F0) {
  Deliver(ctx, base);
  __imp__sub_8207D5F0(ctx, base);
}

std::string bbr::VisualizerStatus() {
  switch (g_mode.load()) {
    case 1: return "listening (game left out)";
    case 2: return "listening (game included)";
    default: return g_enabled.load() ? "starting" : "off";
  }
}
