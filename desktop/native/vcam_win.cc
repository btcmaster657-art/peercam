// vcam_win.cc
// Writes RGBA frames into two virtual camera backends:
//   1. PeerCam SHM          → PeerCamVCam.dll (native apps)
//   2. OBSVirtualCamVideo   → OBS Virtual Camera when that filter is installed

#include <napi.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "peercam_shm.h"

// ── PeerCam DirectShow channel ────────────────────────────────────────────────
static HANDLE g_pcMapFile = nullptr;
static HANDLE g_pcEvent   = nullptr;
static HANDLE g_pcMutex   = nullptr;
static LPVOID g_pcShm     = nullptr;

// ── OBS queue channel ────────────────────────────────────────────────────────
static HANDLE g_obsMapFile = nullptr;
static LPVOID g_obsQueue   = nullptr;
static bool   g_obsActive  = false;
static DWORD  g_obsWidth   = 0;
static DWORD  g_obsHeight  = 0;

static bool   g_running    = false;

static bool RegistryKeyExists(HKEY root, const wchar_t* subKey) {
    HKEY key = nullptr;
    const LONG status = RegOpenKeyExW(root, subKey, 0, KEY_READ, &key);
    if (status == ERROR_SUCCESS && key) {
        RegCloseKey(key);
        return true;
    }
    return false;
}

static bool IsObsVirtualCameraRegistered() {
    return RegistryKeyExists(
        HKEY_CLASSES_ROOT,
        L"CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance\\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}"
    );
}

static inline uint8_t ClampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

static inline uint8_t RgbToY(uint8_t r, uint8_t g, uint8_t b) {
    return ClampByte(static_cast<int>(0.299f * r + 0.587f * g + 0.114f * b));
}

static inline uint8_t RgbToU(uint8_t r, uint8_t g, uint8_t b) {
    return ClampByte(static_cast<int>(128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b));
}

static inline uint8_t RgbToV(uint8_t r, uint8_t g, uint8_t b) {
    return ClampByte(static_cast<int>(128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b));
}

static void RgbaToNv12(const uint8_t* rgba, uint8_t* nv12, DWORD width, DWORD height) {
    const size_t planeSize = static_cast<size_t>(width) * height;
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + planeSize;

    for (DWORD y = 0; y < height; y += 2) {
        for (DWORD x = 0; x < width; x += 2) {
            const size_t idx00 = static_cast<size_t>(y) * width + x;
            const size_t idx01 = idx00 + 1;
            const size_t idx10 = idx00 + width;
            const size_t idx11 = idx10 + 1;

            const uint8_t* p00 = rgba + idx00 * 4;
            const uint8_t* p01 = rgba + idx01 * 4;
            const uint8_t* p10 = rgba + idx10 * 4;
            const uint8_t* p11 = rgba + idx11 * 4;

            yPlane[idx00] = RgbToY(p00[0], p00[1], p00[2]);
            yPlane[idx01] = RgbToY(p01[0], p01[1], p01[2]);
            yPlane[idx10] = RgbToY(p10[0], p10[1], p10[2]);
            yPlane[idx11] = RgbToY(p11[0], p11[1], p11[2]);

            const int u = (
                RgbToU(p00[0], p00[1], p00[2]) +
                RgbToU(p01[0], p01[1], p01[2]) +
                RgbToU(p10[0], p10[1], p10[2]) +
                RgbToU(p11[0], p11[1], p11[2])
            ) / 4;
            const int v = (
                RgbToV(p00[0], p00[1], p00[2]) +
                RgbToV(p01[0], p01[1], p01[2]) +
                RgbToV(p10[0], p10[1], p10[2]) +
                RgbToV(p11[0], p11[1], p11[2])
            ) / 4;

            const size_t uvIndex = (static_cast<size_t>(y) / 2) * width + x;
            uvPlane[uvIndex] = static_cast<uint8_t>(u);
            uvPlane[uvIndex + 1] = static_cast<uint8_t>(v);
        }
    }
}

static void StopObsQueue() {
    if (g_obsQueue) {
        auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
        header->state = OBS_QUEUE_STOPPING;
        UnmapViewOfFile(g_obsQueue);
        g_obsQueue = nullptr;
    }
    if (g_obsMapFile) {
        CloseHandle(g_obsMapFile);
        g_obsMapFile = nullptr;
    }
    g_obsWidth = 0;
    g_obsHeight = 0;
}

static bool EnsureObsQueue(DWORD width, DWORD height) {
    if (!g_obsActive) {
        return false;
    }
    if (g_obsQueue && g_obsWidth == width && g_obsHeight == height) {
        return true;
    }

    StopObsQueue();

    HANDLE existing = OpenFileMappingW(FILE_MAP_READ, FALSE, OBS_VCAM_QUEUE_NAME);
    if (existing) {
        CloseHandle(existing);
        return false;
    }

    const size_t queueSize = ObsQueueSize(width, height);
    g_obsMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>(queueSize >> 32),
        static_cast<DWORD>(queueSize & 0xffffffff),
        OBS_VCAM_QUEUE_NAME
    );
    if (!g_obsMapFile || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_obsMapFile) {
            CloseHandle(g_obsMapFile);
            g_obsMapFile = nullptr;
        }
        return false;
    }

    g_obsQueue = MapViewOfFile(g_obsMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_obsQueue) {
        CloseHandle(g_obsMapFile);
        g_obsMapFile = nullptr;
        return false;
    }

    auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
    ZeroMemory(header, queueSize);
    header->state = OBS_QUEUE_STARTING;
    header->type = 0;
    header->width = width;
    header->height = height;
    header->interval100ns = OBS_VCAM_INTERVAL_30FPS;

    size_t offset = AlignObsSize(sizeof(ObsQueueHeader));
    const size_t frameSize = ObsFrameSize(width, height);
    for (int i = 0; i < OBS_VCAM_BUFFER_COUNT; i++) {
        header->offsets[i] = static_cast<uint32_t>(offset);
        offset += frameSize + OBS_VCAM_FRAME_HEADER;
        offset = AlignObsSize(offset);
    }

    g_obsWidth = width;
    g_obsHeight = height;
    return true;
}

Napi::Value Start(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (g_running) {
        auto r = Napi::Object::New(env);
        r.Set("ok",  Napi::Boolean::New(env, true));
        r.Set("obs", Napi::Boolean::New(env, g_obsActive));
        return r;
    }

    // ── PeerCam DirectShow SHM ────────────────────────────────────────────────
    g_pcMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(PEERCAM_SHM_SIZE), PEERCAM_SHM_NAME
    );
    if (!g_pcMapFile) {
        auto r = Napi::Object::New(env);
        r.Set("ok",  Napi::Boolean::New(env, false));
        r.Set("obs", Napi::Boolean::New(env, false));
        return r;
    }
    g_pcShm = MapViewOfFile(g_pcMapFile, FILE_MAP_ALL_ACCESS, 0, 0, PEERCAM_SHM_SIZE);
    if (!g_pcShm) {
        CloseHandle(g_pcMapFile); g_pcMapFile = nullptr;
        auto r = Napi::Object::New(env);
        r.Set("ok",  Napi::Boolean::New(env, false));
        r.Set("obs", Napi::Boolean::New(env, false));
        return r;
    }
    ZeroMemory(g_pcShm, sizeof(PeerCamShmHeader));
    g_pcEvent = CreateEventA(nullptr, FALSE, FALSE, PEERCAM_EVENT_NAME);
    g_pcMutex = CreateMutexA(nullptr, FALSE, PEERCAM_MUTEX_NAME);

    g_obsActive = IsObsVirtualCameraRegistered();

    g_running = true;
    auto r = Napi::Object::New(env);
    r.Set("ok",  Napi::Boolean::New(env, true));
    r.Set("obs", Napi::Boolean::New(env, g_obsActive));
    return r;
}

void Stop(const Napi::CallbackInfo&) {
    if (g_pcEvent)    { CloseHandle(g_pcEvent);    g_pcEvent   = nullptr; }
    if (g_pcMutex)    { CloseHandle(g_pcMutex);    g_pcMutex   = nullptr; }
    if (g_pcShm)      { UnmapViewOfFile(g_pcShm);  g_pcShm     = nullptr; }
    if (g_pcMapFile)  { CloseHandle(g_pcMapFile);  g_pcMapFile = nullptr; }
    StopObsQueue();
    g_obsActive = false;
    g_running   = false;
}

Napi::Value PushFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_running || !g_pcShm) return env.Undefined();

    DWORD width  = info[0].As<Napi::Number>().Uint32Value();
    DWORD height = info[1].As<Napi::Number>().Uint32Value();
    Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

    size_t pixelBytes = static_cast<size_t>(width) * height * 4;
    if (width > PEERCAM_MAX_WIDTH || height > PEERCAM_MAX_HEIGHT) return env.Undefined();
    if (buf.ByteLength() < pixelBytes) return env.Undefined();

    const uint8_t* rgba = buf.Data();

    // ── PeerCam DirectShow SHM ────────────────────────────────────────────────
    if (g_pcMutex) WaitForSingleObject(g_pcMutex, 16);
    PeerCamShmHeader* pcHdr = reinterpret_cast<PeerCamShmHeader*>(g_pcShm);
    pcHdr->width  = width;
    pcHdr->height = height;
    pcHdr->frameCount++;
    std::memcpy(PeerCamPixelData(g_pcShm), rgba, pixelBytes);
    if (g_pcMutex) ReleaseMutex(g_pcMutex);
    if (g_pcEvent) SetEvent(g_pcEvent);

    // ── OBS Virtual Camera queue ────────────────────────────────────────────
    if (EnsureObsQueue(width, height)) {
        auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
        const uint32_t nextIndex = header->writeIndex + 1;
        const uint32_t slot = nextIndex % OBS_VCAM_BUFFER_COUNT;
        auto* timestamp = reinterpret_cast<uint64_t*>(
            reinterpret_cast<BYTE*>(g_obsQueue) + header->offsets[slot]
        );
        uint8_t* frame = ObsFrameData(g_obsQueue, header->offsets[slot]);

        *timestamp = static_cast<uint64_t>(GetTickCount64()) * 10000ULL;
        RgbaToNv12(rgba, frame, width, height);

        MemoryBarrier();
        header->writeIndex = nextIndex;
        header->readIndex = nextIndex;
        header->state = OBS_QUEUE_READY;
    }

    return env.Undefined();
}
