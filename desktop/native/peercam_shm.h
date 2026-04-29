#pragma once
#include <windows.h>

// PeerCam virtual camera shared memory protocol
// Both vcam_win.cc (writer) and PeerCamVCam.dll (reader) use this layout.

#define PEERCAM_SHM_NAME    "PeerCamVCam"
#define PEERCAM_EVENT_NAME  "PeerCamVCamEvent"   // SetEvent() after each frame write
#define PEERCAM_MUTEX_NAME  "PeerCamVCamMutex"   // protects header during write

// Max supported resolution: 1920x1080 RGBA
#define PEERCAM_MAX_WIDTH   1920
#define PEERCAM_MAX_HEIGHT  1080
#define PEERCAM_MAX_PIXELS  (PEERCAM_MAX_WIDTH * PEERCAM_MAX_HEIGHT)
#define PEERCAM_SHM_SIZE    (sizeof(PeerCamShmHeader) + PEERCAM_MAX_PIXELS * 4)

#pragma pack(push, 1)
struct PeerCamShmHeader {
    DWORD width;        // current frame width
    DWORD height;       // current frame height
    DWORD frameCount;   // incremented on each write — reader detects new frames
    DWORD reserved;
    // RGBA pixel data follows immediately after this header
};
#pragma pack(pop)

inline BYTE* PeerCamPixelData(void* shmBase) {
    return reinterpret_cast<BYTE*>(shmBase) + sizeof(PeerCamShmHeader);
}
