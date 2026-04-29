#pragma once
#include <windows.h>
#include <stdint.h>

// ── PeerCam DirectShow shared memory ─────────────────────────────────────────
#define PEERCAM_SHM_NAME    "PeerCamVCam"
#define PEERCAM_EVENT_NAME  "PeerCamVCamEvent"
#define PEERCAM_MUTEX_NAME  "PeerCamVCamMutex"

#define PEERCAM_MAX_WIDTH   1920
#define PEERCAM_MAX_HEIGHT  1080
#define PEERCAM_MAX_PIXELS  (PEERCAM_MAX_WIDTH * PEERCAM_MAX_HEIGHT)
#define PEERCAM_SHM_SIZE    (sizeof(PeerCamShmHeader) + PEERCAM_MAX_PIXELS * 4)

#pragma pack(push, 1)
struct PeerCamShmHeader {
    DWORD width;
    DWORD height;
    DWORD frameCount;
    DWORD reserved;
};
#pragma pack(pop)

inline BYTE* PeerCamPixelData(void* shmBase) {
    return reinterpret_cast<BYTE*>(shmBase) + sizeof(PeerCamShmHeader);
}

// ── OBS Virtual Camera queue ────────────────────────────────────────────────
// The registered OBS virtual camera filter reads NV12 frames from this queue.
#define OBS_VCAM_QUEUE_NAME      L"OBSVirtualCamVideo"
#define OBS_VCAM_FRAME_ALIGN     32
#define OBS_VCAM_FRAME_HEADER    32
#define OBS_VCAM_BUFFER_COUNT    3
#define OBS_VCAM_INTERVAL_30FPS  333333ULL

#pragma pack(push, 1)
struct ObsQueueHeader {
    volatile uint32_t writeIndex;
    volatile uint32_t readIndex;
    volatile uint32_t state;
    uint32_t offsets[OBS_VCAM_BUFFER_COUNT];
    uint32_t type;
    uint32_t width;
    uint32_t height;
    uint64_t interval100ns;
    uint32_t reserved[8];
};
#pragma pack(pop)

enum ObsQueueState : uint32_t {
    OBS_QUEUE_INVALID = 0,
    OBS_QUEUE_STARTING = 1,
    OBS_QUEUE_READY = 2,
    OBS_QUEUE_STOPPING = 3,
};

inline size_t AlignObsSize(size_t size) {
    return (size + (OBS_VCAM_FRAME_ALIGN - 1)) & ~(static_cast<size_t>(OBS_VCAM_FRAME_ALIGN) - 1);
}

inline size_t ObsFrameSize(uint32_t width, uint32_t height) {
    return static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
}

inline size_t ObsQueueSize(uint32_t width, uint32_t height) {
    size_t size = AlignObsSize(sizeof(ObsQueueHeader));
    const size_t frameSize = ObsFrameSize(width, height);
    for (int i = 0; i < OBS_VCAM_BUFFER_COUNT; i++) {
        size += frameSize + OBS_VCAM_FRAME_HEADER;
        size = AlignObsSize(size);
    }
    return size;
}

inline BYTE* ObsFrameData(void* queueBase, uint32_t offset) {
    return reinterpret_cast<BYTE*>(queueBase) + offset + OBS_VCAM_FRAME_HEADER;
}
