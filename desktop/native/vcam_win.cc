// Windows: pushes RGBA frames into OBS Virtual Camera via its shared memory.
// OBS Virtual Camera (ships with OBS Studio) must be installed and active.
//
// OBS obs-virtualcam-module shared memory layout:
//   [0..3]   width  (DWORD, little-endian)
//   [4..7]   height (DWORD, little-endian)
//   [8..]    RGBA pixel data (row-major, top-down)
//
// OBS also creates a named event "OBSVirtualCamEvent" that it waits on.
// We SetEvent() after each frame write so OBS picks it up immediately.

#include <napi.h>
#include <windows.h>
#include <cstring>

static HANDLE g_hMapFile  = nullptr;
static LPVOID g_pBuf      = nullptr;
static HANDLE g_hEvent    = nullptr;
static bool   g_running   = false;
static bool   g_ownedMap  = false; // true if we created the mapping (OBS not running)

static const char* SHM_NAME   = "OBSVirtualCam";
static const char* EVENT_NAME = "OBSVirtualCamEvent";
// Max frame size: 1920×1080 RGBA + 8-byte header
static const DWORD SHM_SIZE   = 8 + 1920 * 1080 * 4;

Napi::Value Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (g_running) return Napi::Boolean::New(env, true);

  // Try to open OBS's existing mapping first
  g_hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
  if (g_hMapFile) {
    g_ownedMap = false;
  } else {
    // OBS not running — create our own so the device still works if a
    // custom DirectShow filter reads from the same name
    g_hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE, 0, SHM_SIZE, SHM_NAME);
    if (!g_hMapFile) return Napi::Boolean::New(env, false);
    g_ownedMap = true;
  }

  g_pBuf = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
  if (!g_pBuf) {
    CloseHandle(g_hMapFile);
    g_hMapFile = nullptr;
    return Napi::Boolean::New(env, false);
  }

  // Open the OBS frame-ready event (may not exist if OBS isn't running — that's fine)
  g_hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);

  g_running = true;
  return Napi::Boolean::New(env, true);
}

void Stop(const Napi::CallbackInfo&) {
  if (g_hEvent)   { CloseHandle(g_hEvent);   g_hEvent = nullptr; }
  if (g_pBuf)     { UnmapViewOfFile(g_pBuf); g_pBuf = nullptr; }
  if (g_hMapFile) { CloseHandle(g_hMapFile); g_hMapFile = nullptr; }
  g_running  = false;
  g_ownedMap = false;
}

Napi::Value PushFrame(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!g_running || !g_pBuf) return env.Undefined();

  uint32_t width  = info[0].As<Napi::Number>().Uint32Value();
  uint32_t height = info[1].As<Napi::Number>().Uint32Value();
  Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

  size_t pixelBytes = static_cast<size_t>(width) * height * 4;
  if (pixelBytes + 8 > SHM_SIZE) return env.Undefined();
  if (buf.ByteLength() < pixelBytes) return env.Undefined();

  DWORD* header = reinterpret_cast<DWORD*>(g_pBuf);
  header[0] = static_cast<DWORD>(width);
  header[1] = static_cast<DWORD>(height);
  std::memcpy(reinterpret_cast<uint8_t*>(g_pBuf) + 8, buf.Data(), pixelBytes);

  // Signal OBS to consume the new frame
  if (g_hEvent) SetEvent(g_hEvent);

  return env.Undefined();
}
