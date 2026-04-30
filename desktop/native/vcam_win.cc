// // vcam_win.cc
// // Writes RGBA frames into two virtual camera backends:
// //   1. PeerCam SHM          → PeerCamVCam.dll (native apps)
// //   2. OBSVirtualCamVideo   → OBS Virtual Camera when that filter is installed

// #include <napi.h>
// #include <windows.h>
// #include <cstdint>
// #include <cstring>
// #include <cstdlib>
// #include "peercam_shm.h"

// #define DEFAULT_STANDBY_WIDTH  640
// #define DEFAULT_STANDBY_HEIGHT 480

// // ── PeerCam DirectShow channel ────────────────────────────────────────────────
// static HANDLE g_pcMapFile = nullptr;
// static HANDLE g_pcEvent   = nullptr;
// static HANDLE g_pcMutex   = nullptr;
// static LPVOID g_pcShm     = nullptr;

// // ── OBS queue channel ────────────────────────────────────────────────────────
// static HANDLE g_obsMapFile = nullptr;
// static LPVOID g_obsQueue   = nullptr;
// static bool   g_obsActive  = false;
// static DWORD  g_obsWidth   = 0;
// static DWORD  g_obsHeight  = 0;

// static bool   g_running    = false;

// // ── Standby frame ─────────────────────────────────────────────────────────────
// // Minimal 5×7 bitmap font (printable ASCII 32–126)
// static const uint8_t kFont5x7[][7] = {
//     {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
//     {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // '!'
//     {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // '"'
//     {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // '#'
//     {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
//     {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
//     {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
//     {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '\''
//     {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
//     {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
//     {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
//     {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
//     {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, // ','
//     {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
//     {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // '.'
//     {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // '/'
//     {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
//     {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
//     {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // '2'
//     {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // '3'
//     {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
//     {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
//     {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
//     {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
//     {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
//     {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
//     {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, // ':'
//     {0x00,0x04,0x00,0x00,0x00,0x04,0x08}, // ';'
//     {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
//     {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
//     {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
//     {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // '?'
//     {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // '@'
//     {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'A'
//     {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
//     {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
//     {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
//     {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
//     {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
//     {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
//     {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
//     {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
//     {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
//     {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
//     {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
//     {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
//     {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 'N'
//     {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
//     {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
//     {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
//     {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
//     {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
//     {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
//     {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
//     {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
//     {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 'W'
//     {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
//     {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
//     {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
//     {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
//     {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // '\\'
//     {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
//     {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
//     {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
//     {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // '`'
//     {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 'a'
//     {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 'b'
//     {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, // 'c'
//     {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 'd'
//     {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 'e'
//     {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 'f'
//     {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, // 'g'
//     {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 'h'
//     {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 'i'
//     {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 'j'
//     {0x10,0x10,0x11,0x12,0x1C,0x12,0x11}, // 'k'
//     {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'l'
//     {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 'm'
//     {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 'n'
//     {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 'o'
//     {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 'p'
//     {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, // 'q'
//     {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 'r'
//     {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 's'
//     {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 't'
//     {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, // 'u'
//     {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 'v'
//     {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, // 'w'
//     {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 'x'
//     {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 'y'
//     {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 'z'
//     {0x06,0x08,0x08,0x18,0x08,0x08,0x06}, // '{'
//     {0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // '|'
//     {0x0C,0x02,0x02,0x03,0x02,0x02,0x0C}, // '}'
//     {0x08,0x15,0x02,0x00,0x00,0x00,0x00}, // '~'
// };

// static void DrawChar(uint8_t* rgba, int imgW, int imgH, int cx, int cy,
//                      char ch, uint8_t r, uint8_t g, uint8_t b) {
//     if (ch < 32 || ch > 126) return;
//     const uint8_t* glyph = kFont5x7[ch - 32];
//     const int scale = 2;
//     for (int row = 0; row < 7; row++) {
//         for (int col = 0; col < 5; col++) {
//             if ((glyph[row] & (0x10 >> col)) == 0) continue;
//             for (int sy = 0; sy < scale; sy++) {
//                 for (int sx = 0; sx < scale; sx++) {
//                     int px = cx + col * scale + sx;
//                     int py = cy + row * scale + sy;
//                     if (px < 0 || px >= imgW || py < 0 || py >= imgH) continue;
//                     uint8_t* p = rgba + (py * imgW + px) * 4;
//                     p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF;
//                 }
//             }
//         }
//     }
// }

// static void DrawString(uint8_t* rgba, int imgW, int imgH, int x, int y,
//                        const char* text, uint8_t r, uint8_t g, uint8_t b) {
//     const int charW = 5 * 2 + 2; // scale=2, 1px gap
//     for (int i = 0; text[i]; i++)
//         DrawChar(rgba, imgW, imgH, x + i * charW, y, text[i], r, g, b);
// }

// static void WriteStandbyFrame(void* shmBase) {
//     const int W = DEFAULT_STANDBY_WIDTH;
//     const int H = DEFAULT_STANDBY_HEIGHT;

//     PeerCamShmHeader* hdr = reinterpret_cast<PeerCamShmHeader*>(shmBase);
//     hdr->width      = W;
//     hdr->height     = H;
//     hdr->frameCount = 1;
//     hdr->reserved   = 0;

//     uint8_t* rgba = PeerCamPixelData(shmBase);
//     // Dark background #1a1a2e
//     for (int i = 0; i < W * H; i++) {
//         rgba[i*4+0] = 0x1A;
//         rgba[i*4+1] = 0x1A;
//         rgba[i*4+2] = 0x2E;
//         rgba[i*4+3] = 0xFF;
//     }

//     // Draw a simple camera icon outline (circle + lens)
//     const int cx = W / 2, cy = H / 2 - 30;
//     const int outerR = 28, innerR = 16;
//     const int outer2    = outerR * outerR;
//     const int inner2    = innerR * innerR;
//     const int innerRing = (outerR - 3) * (outerR - 3);
//     for (int y = cy - outerR - 2; y <= cy + outerR + 2; y++) {
//         for (int x = cx - outerR - 2; x <= cx + outerR + 2; x++) {
//             if (x < 0 || x >= W || y < 0 || y >= H) continue;
//             const int dx    = x - cx;
//             const int dy    = y - cy;
//             const int dist2 = (dx * dx) + (dy * dy);
//             if ((dist2 <= outer2) && (dist2 >= innerRing)) {
//                 uint8_t* p = rgba + (y * W + x) * 4;
//                 p[0] = 0x63; p[1] = 0x63; p[2] = 0xD8; p[3] = 0xFF;
//             }
//             if (dist2 <= inner2) {
//                 uint8_t* p = rgba + (y * W + x) * 4;
//                 p[0] = 0x4F; p[1] = 0x46; p[2] = 0xE5; p[3] = 0xFF;
//             }
//         }
//     }

//     // "Waiting for connection..."
//     const char* line1 = "Waiting for";
//     const char* line2 = "connection...";
//     const int charW = 5 * 2 + 2;
//     const int charH = 7 * 2;
//     int x1 = (W - (int)strlen(line1) * charW) / 2;
//     int x2 = (W - (int)strlen(line2) * charW) / 2;
//     int textY = cy + outerR + 16;
//     DrawString(rgba, W, H, x1, textY,          line1, 0xA1, 0xA1, 0xAA);
//     DrawString(rgba, W, H, x2, textY + charH + 4, line2, 0xA1, 0xA1, 0xAA);

//     // "PeerCam" label at bottom
//     const char* brand = "PeerCam";
//     int bx = (W - (int)strlen(brand) * charW) / 2;
//     DrawString(rgba, W, H, bx, H - charH - 16, brand, 0x63, 0x63, 0xD8);
// }

// static bool RegistryKeyExists(HKEY root, const wchar_t* subKey) {
//     HKEY key = nullptr;
//     const LONG status = RegOpenKeyExW(root, subKey, 0, KEY_READ, &key);
//     if (status == ERROR_SUCCESS && key) {
//         RegCloseKey(key);
//         return true;
//     }
//     return false;
// }

// static bool IsObsVirtualCameraRegistered() {
//     return RegistryKeyExists(
//         HKEY_CLASSES_ROOT,
//         L"CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance\\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}"
//     );
// }

// static inline uint8_t ClampByte(int value) {
//     if (value < 0) return 0;
//     if (value > 255) return 255;
//     return static_cast<uint8_t>(value);
// }

// static inline uint8_t RgbToY(uint8_t r, uint8_t g, uint8_t b) {
//     return ClampByte(static_cast<int>(0.299f * r + 0.587f * g + 0.114f * b));
// }

// static inline uint8_t RgbToU(uint8_t r, uint8_t g, uint8_t b) {
//     return ClampByte(static_cast<int>(128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b));
// }

// static inline uint8_t RgbToV(uint8_t r, uint8_t g, uint8_t b) {
//     return ClampByte(static_cast<int>(128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b));
// }

// static void RgbaToNv12(const uint8_t* rgba, uint8_t* nv12, DWORD width, DWORD height) {
//     const size_t planeSize = static_cast<size_t>(width) * height;
//     uint8_t* yPlane = nv12;
//     uint8_t* uvPlane = nv12 + planeSize;

//     for (DWORD y = 0; y < height; y += 2) {
//         for (DWORD x = 0; x < width; x += 2) {
//             const size_t idx00 = static_cast<size_t>(y) * width + x;
//             const size_t idx01 = idx00 + 1;
//             const size_t idx10 = idx00 + width;
//             const size_t idx11 = idx10 + 1;

//             const uint8_t* p00 = rgba + idx00 * 4;
//             const uint8_t* p01 = rgba + idx01 * 4;
//             const uint8_t* p10 = rgba + idx10 * 4;
//             const uint8_t* p11 = rgba + idx11 * 4;

//             yPlane[idx00] = RgbToY(p00[0], p00[1], p00[2]);
//             yPlane[idx01] = RgbToY(p01[0], p01[1], p01[2]);
//             yPlane[idx10] = RgbToY(p10[0], p10[1], p10[2]);
//             yPlane[idx11] = RgbToY(p11[0], p11[1], p11[2]);

//             const int u = (
//                 RgbToU(p00[0], p00[1], p00[2]) +
//                 RgbToU(p01[0], p01[1], p01[2]) +
//                 RgbToU(p10[0], p10[1], p10[2]) +
//                 RgbToU(p11[0], p11[1], p11[2])
//             ) / 4;
//             const int v = (
//                 RgbToV(p00[0], p00[1], p00[2]) +
//                 RgbToV(p01[0], p01[1], p01[2]) +
//                 RgbToV(p10[0], p10[1], p10[2]) +
//                 RgbToV(p11[0], p11[1], p11[2])
//             ) / 4;

//             const size_t uvIndex = (static_cast<size_t>(y) / 2) * width + x;
//             uvPlane[uvIndex] = static_cast<uint8_t>(u);
//             uvPlane[uvIndex + 1] = static_cast<uint8_t>(v);
//         }
//     }
// }

// static void StopObsQueue() {
//     if (g_obsQueue) {
//         auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
//         header->state = OBS_QUEUE_STOPPING;
//         UnmapViewOfFile(g_obsQueue);
//         g_obsQueue = nullptr;
//     }
//     if (g_obsMapFile) {
//         CloseHandle(g_obsMapFile);
//         g_obsMapFile = nullptr;
//     }
//     g_obsWidth = 0;
//     g_obsHeight = 0;
// }

// static bool EnsureObsQueue(DWORD width, DWORD height) {
//     if (!g_obsActive) {
//         return false;
//     }
//     if (g_obsQueue && g_obsWidth == width && g_obsHeight == height) {
//         return true;
//     }

//     StopObsQueue();

//     HANDLE existing = OpenFileMappingW(FILE_MAP_READ, FALSE, OBS_VCAM_QUEUE_NAME);
//     if (existing) {
//         CloseHandle(existing);
//         return false;
//     }

//     const size_t queueSize = ObsQueueSize(width, height);
//     g_obsMapFile = CreateFileMappingW(
//         INVALID_HANDLE_VALUE,
//         nullptr,
//         PAGE_READWRITE,
//         static_cast<DWORD>(queueSize >> 32),
//         static_cast<DWORD>(queueSize & 0xffffffff),
//         OBS_VCAM_QUEUE_NAME
//     );
//     if (!g_obsMapFile || GetLastError() == ERROR_ALREADY_EXISTS) {
//         if (g_obsMapFile) {
//             CloseHandle(g_obsMapFile);
//             g_obsMapFile = nullptr;
//         }
//         return false;
//     }

//     g_obsQueue = MapViewOfFile(g_obsMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
//     if (!g_obsQueue) {
//         CloseHandle(g_obsMapFile);
//         g_obsMapFile = nullptr;
//         return false;
//     }

//     auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
//     ZeroMemory(header, queueSize);
//     header->state = OBS_QUEUE_STARTING;
//     header->type = 0;
//     header->width = width;
//     header->height = height;
//     header->interval100ns = OBS_VCAM_INTERVAL_30FPS;

//     size_t offset = AlignObsSize(sizeof(ObsQueueHeader));
//     const size_t frameSize = ObsFrameSize(width, height);
//     for (int i = 0; i < OBS_VCAM_BUFFER_COUNT; i++) {
//         header->offsets[i] = static_cast<uint32_t>(offset);
//         offset += frameSize + OBS_VCAM_FRAME_HEADER;
//         offset = AlignObsSize(offset);
//     }

//     g_obsWidth = width;
//     g_obsHeight = height;
//     return true;
// }

// Napi::Value Start(const Napi::CallbackInfo& info) {
//     Napi::Env env = info.Env();
//     if (g_running) {
//         auto r = Napi::Object::New(env);
//         r.Set("ok",  Napi::Boolean::New(env, true));
//         r.Set("obs", Napi::Boolean::New(env, g_obsActive));
//         return r;
//     }

//     // ── PeerCam DirectShow SHM ────────────────────────────────────────────────
//     g_pcMapFile = CreateFileMappingA(
//         INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
//         0, static_cast<DWORD>(PEERCAM_SHM_SIZE), PEERCAM_SHM_NAME
//     );
//     if (!g_pcMapFile) {
//         auto r = Napi::Object::New(env);
//         r.Set("ok",  Napi::Boolean::New(env, false));
//         r.Set("obs", Napi::Boolean::New(env, false));
//         return r;
//     }
//     g_pcShm = MapViewOfFile(g_pcMapFile, FILE_MAP_ALL_ACCESS, 0, 0, PEERCAM_SHM_SIZE);
//     if (!g_pcShm) {
//         CloseHandle(g_pcMapFile); g_pcMapFile = nullptr;
//         auto r = Napi::Object::New(env);
//         r.Set("ok",  Napi::Boolean::New(env, false));
//         r.Set("obs", Napi::Boolean::New(env, false));
//         return r;
//     }
//     WriteStandbyFrame(g_pcShm);
//     g_pcEvent = CreateEventA(nullptr, FALSE, FALSE, PEERCAM_EVENT_NAME);
//     g_pcMutex = CreateMutexA(nullptr, FALSE, PEERCAM_MUTEX_NAME);

//     g_obsActive = IsObsVirtualCameraRegistered();

//     g_running = true;
//     auto r = Napi::Object::New(env);
//     r.Set("ok",  Napi::Boolean::New(env, true));
//     r.Set("obs", Napi::Boolean::New(env, g_obsActive));
//     return r;
// }

// void Stop(const Napi::CallbackInfo&) {
//     if (g_pcEvent)    { CloseHandle(g_pcEvent);    g_pcEvent   = nullptr; }
//     if (g_pcMutex)    { CloseHandle(g_pcMutex);    g_pcMutex   = nullptr; }
//     if (g_pcShm)      { UnmapViewOfFile(g_pcShm);  g_pcShm     = nullptr; }
//     if (g_pcMapFile)  { CloseHandle(g_pcMapFile);  g_pcMapFile = nullptr; }
//     StopObsQueue();
//     g_obsActive = false;
//     g_running   = false;
// }

// Napi::Value PushFrame(const Napi::CallbackInfo& info) {
//     Napi::Env env = info.Env();
//     if (!g_running || !g_pcShm) return env.Undefined();

//     DWORD width  = info[0].As<Napi::Number>().Uint32Value();
//     DWORD height = info[1].As<Napi::Number>().Uint32Value();
//     Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

//     size_t pixelBytes = static_cast<size_t>(width) * height * 4;
//     if (width > PEERCAM_MAX_WIDTH || height > PEERCAM_MAX_HEIGHT) return env.Undefined();
//     if (buf.ByteLength() < pixelBytes) return env.Undefined();

//     const uint8_t* rgba = buf.Data();

//     // ── PeerCam DirectShow SHM ────────────────────────────────────────────────
//     const bool locked = !g_pcMutex || WaitForSingleObject(g_pcMutex, 16) == WAIT_OBJECT_0;
//     if (!locked) return env.Undefined();
//     PeerCamShmHeader* pcHdr = reinterpret_cast<PeerCamShmHeader*>(g_pcShm);
//     pcHdr->width  = width;
//     pcHdr->height = height;
//     pcHdr->frameCount++;
//     std::memcpy(PeerCamPixelData(g_pcShm), rgba, pixelBytes);
//     if (g_pcMutex) ReleaseMutex(g_pcMutex);
//     if (g_pcEvent) SetEvent(g_pcEvent);

//     // ── OBS Virtual Camera queue ────────────────────────────────────────────
//     if (EnsureObsQueue(width, height)) {
//         auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
//         const uint32_t nextIndex = header->writeIndex + 1;
//         const uint32_t slot = nextIndex % OBS_VCAM_BUFFER_COUNT;
//         auto* timestamp = reinterpret_cast<uint64_t*>(
//             reinterpret_cast<BYTE*>(g_obsQueue) + header->offsets[slot]
//         );
//         uint8_t* frame = ObsFrameData(g_obsQueue, header->offsets[slot]);

//         *timestamp = static_cast<uint64_t>(GetTickCount64()) * 10000ULL;
//         RgbaToNv12(rgba, frame, width, height);

//         MemoryBarrier();
//         header->writeIndex = nextIndex;
//         header->readIndex = nextIndex;
//         header->state = OBS_QUEUE_READY;
//     }

//     return env.Undefined();
// }

// vcam_win.cc
// Writes RGBA frames into two virtual camera backends:
//   1. PeerCam SHM          → PeerCamVCam.dll (native apps)
//   2. OBSVirtualCamVideo   → OBS Virtual Camera when that filter is installed

#include <napi.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "peercam_shm.h"

#define DEFAULT_STANDBY_WIDTH  640
#define DEFAULT_STANDBY_HEIGHT 480

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

// ── Standby frame ─────────────────────────────────────────────────────────────
// Minimal 5×7 bitmap font (printable ASCII 32–126)
static const uint8_t kFont5x7[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // '!'
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, // ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // '.'
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, // '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // '2'
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, // ':'
    {0x00,0x04,0x00,0x00,0x00,0x04,0x08}, // ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // '@'
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, // '\\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 'a'
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 'b'
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, // 'c'
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 'd'
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 'e'
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 'f'
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, // 'g'
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 'h'
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 'i'
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 'j'
    {0x10,0x10,0x11,0x12,0x1C,0x12,0x11}, // 'k'
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'l'
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 'm'
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 'n'
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 'o'
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 'p'
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, // 'q'
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 'r'
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 's'
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 't'
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, // 'u'
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 'v'
    {0x00,0x00,0x11,0x15,0x15,0x15,0x0A}, // 'w'
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 'x'
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 'y'
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 'z'
    {0x06,0x08,0x08,0x18,0x08,0x08,0x06}, // '{'
    {0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // '|'
    {0x0C,0x02,0x02,0x03,0x02,0x02,0x0C}, // '}'
    {0x08,0x15,0x02,0x00,0x00,0x00,0x00}, // '~'
};

static void DrawChar(uint8_t* rgba, int imgW, int imgH, int cx, int cy,
                     char ch, uint8_t r, uint8_t g, uint8_t b) {
    if (ch < 32 || ch > 126) return;
    const uint8_t* glyph = kFont5x7[ch - 32];
    const int scale = 2;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if ((glyph[row] & (0x10 >> col)) == 0) continue;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int px = cx + col * scale + sx;
                    int py = cy + row * scale + sy;
                    if (px < 0 || px >= imgW || py < 0 || py >= imgH) continue;
                    uint8_t* p = rgba + (py * imgW + px) * 4;
                    p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF;
                }
            }
        }
    }
}

static void DrawString(uint8_t* rgba, int imgW, int imgH, int x, int y,
                       const char* text, uint8_t r, uint8_t g, uint8_t b) {
    const int charW = 5 * 2 + 2; // scale=2, 1px gap
    for (int i = 0; text[i]; i++)
        DrawChar(rgba, imgW, imgH, x + i * charW, y, text[i], r, g, b);
}

// ── WriteStandbyFrame ─────────────────────────────────────────────────────────
// Writes the "Waiting for connection..." frame at any requested resolution.
// Always written TOP-DOWN (row 0 = top) so it matches the orientation of live
// RGBA frames pushed via PushFrame. The DShow filter is responsible for any
// bottom-up flip required by its downstream sink — we must not pre-flip here
// because some sinks (OBS, browser-based consumers) expect top-down and would
// display the frame upside-down if we flipped.
static void WriteStandbyFrame(void* shmBase, int W, int H) {
    PeerCamShmHeader* hdr = reinterpret_cast<PeerCamShmHeader*>(shmBase);
    hdr->width      = static_cast<DWORD>(W);
    hdr->height     = static_cast<DWORD>(H);
    hdr->frameCount = 1;
    hdr->reserved   = 0;

    uint8_t* rgba = PeerCamPixelData(shmBase);

    // Fill background #1a1a2e — top-down, no vertical flip
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            rgba[i*4+0] = 0x1A;
            rgba[i*4+1] = 0x1A;
            rgba[i*4+2] = 0x2E;
            rgba[i*4+3] = 0xFF;
        }
    }

    // Camera icon — scale the position and radius relative to the frame size
    // so it always appears centred regardless of resolution.
    const int cx = W / 2;
    const int cy = H / 2 - H / 16;
    const int outerR    = W / 23;               // ~28px at 640-wide
    const int innerR    = outerR * 16 / 28;     // proportional lens
    const int outer2    = outerR * outerR;
    const int inner2    = innerR * innerR;
    const int innerRing = (outerR - (outerR / 9 + 1)) * (outerR - (outerR / 9 + 1));

    for (int y = cy - outerR - 2; y <= cy + outerR + 2; y++) {
        for (int x = cx - outerR - 2; x <= cx + outerR + 2; x++) {
            if (x < 0 || x >= W || y < 0 || y >= H) continue;
            const int dx    = x - cx;
            const int dy    = y - cy;
            const int dist2 = dx * dx + dy * dy;
            if (dist2 <= outer2 && dist2 >= innerRing) {
                uint8_t* p = rgba + (y * W + x) * 4;
                p[0] = 0x63; p[1] = 0x63; p[2] = 0xD8; p[3] = 0xFF;
            }
            if (dist2 <= inner2) {
                uint8_t* p = rgba + (y * W + x) * 4;
                p[0] = 0x4F; p[1] = 0x46; p[2] = 0xE5; p[3] = 0xFF;
            }
        }
    }

    const char* line1 = "Waiting for";
    const char* line2 = "connection...";
    const int charW = 5 * 2 + 2;
    const int charH = 7 * 2;
    int x1    = (W - (int)strlen(line1) * charW) / 2;
    int x2    = (W - (int)strlen(line2) * charW) / 2;
    int textY = cy + outerR + 16;
    DrawString(rgba, W, H, x1, textY,              line1, 0xA1, 0xA1, 0xAA);
    DrawString(rgba, W, H, x2, textY + charH + 4,  line2, 0xA1, 0xA1, 0xAA);

    const char* brand = "PeerCam";
    int bx = (W - (int)strlen(brand) * charW) / 2;
    DrawString(rgba, W, H, bx, H - charH - 16, brand, 0x63, 0x63, 0xD8);
}

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
    uint8_t* yPlane  = nv12;
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
            uvPlane[uvIndex]     = static_cast<uint8_t>(u);
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
    g_obsWidth  = 0;
    g_obsHeight = 0;
}

static bool EnsureObsQueue(DWORD width, DWORD height) {
    if (!g_obsActive) return false;
    if (g_obsQueue && g_obsWidth == width && g_obsHeight == height) return true;

    StopObsQueue();

    HANDLE existing = OpenFileMappingW(FILE_MAP_READ, FALSE, OBS_VCAM_QUEUE_NAME);
    if (existing) {
        CloseHandle(existing);
        return false;
    }

    const size_t queueSize = ObsQueueSize(width, height);
    g_obsMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        static_cast<DWORD>(queueSize >> 32),
        static_cast<DWORD>(queueSize & 0xffffffff),
        OBS_VCAM_QUEUE_NAME
    );
    if (!g_obsMapFile || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_obsMapFile) { CloseHandle(g_obsMapFile); g_obsMapFile = nullptr; }
        return false;
    }

    g_obsQueue = MapViewOfFile(g_obsMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_obsQueue) {
        CloseHandle(g_obsMapFile); g_obsMapFile = nullptr;
        return false;
    }

    auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
    ZeroMemory(header, queueSize);
    header->state         = OBS_QUEUE_STARTING;
    header->type          = 0;
    header->width         = width;
    header->height        = height;
    header->interval100ns = OBS_VCAM_INTERVAL_30FPS;

    size_t offset          = AlignObsSize(sizeof(ObsQueueHeader));
    const size_t frameSize = ObsFrameSize(width, height);
    for (int i = 0; i < OBS_VCAM_BUFFER_COUNT; i++) {
        header->offsets[i] = static_cast<uint32_t>(offset);
        offset += frameSize + OBS_VCAM_FRAME_HEADER;
        offset  = AlignObsSize(offset);
    }

    g_obsWidth  = width;
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

    // Write initial standby frame at default 640×480
    WriteStandbyFrame(g_pcShm, DEFAULT_STANDBY_WIDTH, DEFAULT_STANDBY_HEIGHT);

    g_pcEvent  = CreateEventA(nullptr, FALSE, FALSE, PEERCAM_EVENT_NAME);
    g_pcMutex  = CreateMutexA(nullptr, FALSE, PEERCAM_MUTEX_NAME);
    g_obsActive = IsObsVirtualCameraRegistered();
    g_running   = true;

    auto r = Napi::Object::New(env);
    r.Set("ok",  Napi::Boolean::New(env, true));
    r.Set("obs", Napi::Boolean::New(env, g_obsActive));
    return r;
}

void Stop(const Napi::CallbackInfo&) {
    if (g_pcEvent)   { CloseHandle(g_pcEvent);   g_pcEvent   = nullptr; }
    if (g_pcMutex)   { CloseHandle(g_pcMutex);   g_pcMutex   = nullptr; }
    if (g_pcShm)     { UnmapViewOfFile(g_pcShm);  g_pcShm     = nullptr; }
    if (g_pcMapFile) { CloseHandle(g_pcMapFile);  g_pcMapFile = nullptr; }
    StopObsQueue();
    g_obsActive = false;
    g_running   = false;
}

Napi::Value PushFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!g_running || !g_pcShm) return env.Undefined();

    const DWORD width  = info[0].As<Napi::Number>().Uint32Value();
    const DWORD height = info[1].As<Napi::Number>().Uint32Value();
    Napi::Buffer<uint8_t> buf = info[2].As<Napi::Buffer<uint8_t>>();

    const size_t pixelBytes = static_cast<size_t>(width) * height * 4;
    if (width > PEERCAM_MAX_WIDTH || height > PEERCAM_MAX_HEIGHT) return env.Undefined();
    if (buf.ByteLength() < pixelBytes) return env.Undefined();
    // Width and height must be even (NV12 chroma subsampling requirement)
    if ((width & 1) || (height & 1)) return env.Undefined();

    const uint8_t* rgba = buf.Data();

    // ── PeerCam DirectShow SHM ────────────────────────────────────────────
    // Check whether resolution has changed since last frame. If so, write a
    // standby frame at the new size first so the SHM header is always valid
    // before we overwrite it with live pixel data.
    {
        PeerCamShmHeader* pcHdr = reinterpret_cast<PeerCamShmHeader*>(g_pcShm);
        if (pcHdr->width != width || pcHdr->height != height) {
            // Re-stamp standby at new resolution to keep header consistent;
            // the live frame written below will immediately overwrite pixels.
            WriteStandbyFrame(g_pcShm, static_cast<int>(width), static_cast<int>(height));
        }
    }

    const bool locked = !g_pcMutex || WaitForSingleObject(g_pcMutex, 16) == WAIT_OBJECT_0;
    if (!locked) return env.Undefined();

    PeerCamShmHeader* pcHdr = reinterpret_cast<PeerCamShmHeader*>(g_pcShm);
    pcHdr->width  = width;
    pcHdr->height = height;
    pcHdr->frameCount++;
    std::memcpy(PeerCamPixelData(g_pcShm), rgba, pixelBytes);

    if (g_pcMutex) ReleaseMutex(g_pcMutex);
    if (g_pcEvent) SetEvent(g_pcEvent);

    // ── OBS Virtual Camera queue ──────────────────────────────────────────
    if (EnsureObsQueue(width, height)) {
        auto* header = reinterpret_cast<ObsQueueHeader*>(g_obsQueue);
        const uint32_t nextIndex = header->writeIndex + 1;
        const uint32_t slot      = nextIndex % OBS_VCAM_BUFFER_COUNT;
        auto* timestamp = reinterpret_cast<uint64_t*>(
            reinterpret_cast<BYTE*>(g_obsQueue) + header->offsets[slot]
        );
        uint8_t* frame = ObsFrameData(g_obsQueue, header->offsets[slot]);

        *timestamp = static_cast<uint64_t>(GetTickCount64()) * 10000ULL;
        RgbaToNv12(rgba, frame, width, height);

        MemoryBarrier();
        header->writeIndex = nextIndex;
        header->readIndex  = nextIndex;
        header->state      = OBS_QUEUE_READY;
    }

    return env.Undefined();
}

