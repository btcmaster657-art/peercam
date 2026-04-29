// Windows: writes RGBA frames into PeerCam's own shared memory.
// The PeerCamVCam.dll DirectShow filter reads from this memory and
// exposes it as a real webcam device — no OBS required.

#include <napi.h>
#include <windows.h>
#include <cstring>
#include "peercam_shm.h"

static HANDLE g_hMapFile = nullptr;
static HANDLE g_hEvent   = nullptr;
static HANDLE g_hMutex   = nullptr;
static LPVOID g_pShm     = nullptr;
static bool   g_running  = false;

Napi::Value Start(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (g_running) return Napi::Boolean::New(env, true);

    // Create or open the shared memory
    g_hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(PEERCAM_SHM_SIZE), PEERCAM_SHM_NAME
    );
    if (!g_hMapFile) return Napi::Boolean::New(env, false);

    g_pShm = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, PEERCAM_SHM_SIZE);
    if (!g_pShm) {
        CloseHandle(g_hMapFile); g_hMapFile = nullptr;
        return Napi::Boolean::New(env, false);
    }

    // Zero the header so the filter knows no frame is ready yet
    ZeroMemory(g_pShm, sizeof(PeerCamShmHeader));

    // Create event (auto-reset) — SetEvent() wakes the filter's capture thread
    g_hEvent = CreateEventA(nullptr, FALSE, FALSE, PEERCAM_EVENT_NAME);

    // Create mutex for header protection
    g_hMutex = CreateMutexA(nullptr, FALSE, PEERCAM_MUTEX_NAME);

    g_running = true;
    return Napi::Boolean::New(env, true);
}

void Stop(const Napi::CallbackInfo&) {
    if (g_hEvent)   { CloseHandle(g_hEvent);   g_hEvent = nullptr; }
    if (g_hMutex)   { CloseHandle(g_hMutex);   g_hMutex = nullptr; }
    if (g_pShm)     { UnmapViewOfFile(g_pShm); g_pShm = nullptr; }
    if (g_hMapFile) { CloseHandle(g_hMapFile); g_hMapFile = nullptr; }
    g_running = false;
}

Napi::Value PushFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_running || !g_pShm) return env.Undefined();

    DWORD width  = info[0].As<Napi::Number>().Uint32Value();
    DWORD height = info[1].As<Napi::Number>().Uint32Value();
    Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

    size_t pixelBytes = static_cast<size_t>(width) * height * 4;
    if (width > PEERCAM_MAX_WIDTH || height > PEERCAM_MAX_HEIGHT) return env.Undefined();
    if (buf.ByteLength() < pixelBytes) return env.Undefined();

    // Lock, write header + pixels, unlock, signal
    if (g_hMutex) WaitForSingleObject(g_hMutex, 16); // max 16ms wait

    PeerCamShmHeader* hdr = reinterpret_cast<PeerCamShmHeader*>(g_pShm);
    hdr->width  = width;
    hdr->height = height;
    hdr->frameCount++;
    std::memcpy(PeerCamPixelData(g_pShm), buf.Data(), pixelBytes);

    if (g_hMutex) ReleaseMutex(g_hMutex);
    if (g_hEvent) SetEvent(g_hEvent);

    return env.Undefined();
}
