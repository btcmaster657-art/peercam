// PeerCamVCam.cpp
// DirectShow virtual camera source filter.
// Registers as "PeerCam Virtual Camera" in Windows.
// Reads RGBA frames from shared memory written by vcam_win.cc,
// converts to YUY2, and delivers to any DirectShow consumer.
//
// Build: cl /LD /O2 /EHsc PeerCamVCam.cpp ole32.lib oleaut32.lib
//        strmiids.lib uuid.lib kernel32.lib user32.lib /Fe:PeerCamVCam.dll
// Register: regsvr32 PeerCamVCam.dll
// Unregister: regsvr32 /u PeerCamVCam.dll

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <streams.h>
#include <initguid.h>
#include <uuids.h>
#include <algorithm>
#include <cwchar>
#include <cstring>
#include <cmath>
#include "../peercam_shm.h"

// {A5B3C2D1-E4F5-4A6B-8C7D-9E0F1A2B3C4D}
DEFINE_GUID(CLSID_PeerCamVCam,
    0xa5b3c2d1, 0xe4f5, 0x4a6b,
    0x8c, 0x7d, 0x9e, 0x0f, 0x1a, 0x2b, 0x3c, 0x4d);

static const WCHAR FILTER_NAME[] = L"PeerCam Virtual Camera";
static const int   DEFAULT_WIDTH  = 640;
static const int   DEFAULT_HEIGHT = 480;
static const int   DEFAULT_FPS    = 30;
static const DWORD FRAME_STALE_TIMEOUT_MS = 1500;

struct VideoCapability {
    LONG width;
    LONG height;
    LONG fps;
};

static const VideoCapability kCapabilities[] = {
    { 320,  180, 30 }, { 320,  180, 15 },
    { 320,  240, 30 }, { 320,  240, 15 },
    { 424,  240, 30 }, { 424,  240, 15 },
    { 640,  360, 30 }, { 640,  360, 15 },
    { 640,  480, 30 }, { 640,  480, 15 },
    { 848,  480, 30 }, { 848,  480, 15 },
    { 960,  540, 30 }, { 960,  540, 15 },
    { 1280, 720, 30 }, { 1280, 720, 15 },
};
static const size_t kDefaultCapabilityIndex = 8; // 640x480 @ 30fps

// ── RGBA → YUY2 conversion ────────────────────────────────────────────────────
static inline BYTE ClampToByte(float value) {
    if (value < 0.0f) return 0;
    if (value > 255.0f) return 255;
    return static_cast<BYTE>(value);
}

static inline BYTE RgbToY(BYTE r, BYTE g, BYTE b) {
    return ClampToByte((0.299f * r) + (0.587f * g) + (0.114f * b));
}

static inline BYTE RgbToU(BYTE r, BYTE g, BYTE b) {
    return ClampToByte(128.0f - (0.168736f * r) - (0.331264f * g) + (0.5f * b));
}

static inline BYTE RgbToV(BYTE r, BYTE g, BYTE b) {
    return ClampToByte(128.0f + (0.5f * r) - (0.418688f * g) - (0.081312f * b));
}

static void RgbaToYuy2(const BYTE* rgba, BYTE* yuy2, int width, int height) {
    const int pixels = width * height;
    for (int i = 0; i < pixels; i += 2) {
        const BYTE* p0 = rgba + (i * 4);
        const BYTE* p1 = rgba + ((i + 1) * 4);
        const BYTE y0 = RgbToY(p0[0], p0[1], p0[2]);
        const BYTE y1 = RgbToY(p1[0], p1[1], p1[2]);
        const BYTE u  = RgbToU(p0[0], p0[1], p0[2]);
        const BYTE v  = RgbToV(p0[0], p0[1], p0[2]);
        *yuy2++ = y0; *yuy2++ = u; *yuy2++ = y1; *yuy2++ = v;
    }
}

static void FillSolidYuy2(BYTE* yuy2, int width, int height, BYTE r, BYTE g, BYTE b) {
    const BYTE y = RgbToY(r, g, b);
    const BYTE u = RgbToU(r, g, b);
    const BYTE v = RgbToV(r, g, b);
    const int pixels = width * height;
    for (int i = 0; i < pixels; i += 2) {
        *yuy2++ = y; *yuy2++ = u; *yuy2++ = y; *yuy2++ = v;
    }
}

static void ScaleRgbaToYuy2Letterboxed(
    const BYTE* rgbaSrc,
    int srcWidth,
    int srcHeight,
    BYTE* yuy2Dst,
    int dstWidth,
    int dstHeight,
    BYTE bgR,
    BYTE bgG,
    BYTE bgB)
{
    FillSolidYuy2(yuy2Dst, dstWidth, dstHeight, bgR, bgG, bgB);
    if (!rgbaSrc || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
        return;
    }

    const float scale = std::min(
        static_cast<float>(dstWidth) / static_cast<float>(srcWidth),
        static_cast<float>(dstHeight) / static_cast<float>(srcHeight)
    );
    int drawWidth = std::max(2, static_cast<int>(std::floor(srcWidth * scale)));
    int drawHeight = std::max(2, static_cast<int>(std::floor(srcHeight * scale)));
    if (drawWidth & 1) {
        drawWidth -= 1;
    }
    drawWidth = std::max(2, std::min(drawWidth, dstWidth));
    drawHeight = std::max(2, std::min(drawHeight, dstHeight));

    const int offsetX = ((dstWidth - drawWidth) / 2) & ~1;
    const int offsetY = (dstHeight - drawHeight) / 2;

    for (int y = 0; y < drawHeight; ++y) {
        const int dstY = offsetY + y;
        const int srcY = std::min(srcHeight - 1, (y * srcHeight) / drawHeight);
        BYTE* dstRow = yuy2Dst + (dstY * dstWidth * 2);
        for (int x = 0; x < drawWidth; x += 2) {
            const int dstX = offsetX + x;
            const int srcX0 = std::min(srcWidth - 1, (x * srcWidth) / drawWidth);
            const int srcX1 = std::min(srcWidth - 1, ((x + 1) * srcWidth) / drawWidth);

            const BYTE* p0 = rgbaSrc + ((srcY * srcWidth) + srcX0) * 4;
            const BYTE* p1 = rgbaSrc + ((srcY * srcWidth) + srcX1) * 4;
            BYTE* dst = dstRow + (dstX * 2);
            dst[0] = RgbToY(p0[0], p0[1], p0[2]);
            dst[1] = RgbToU(p0[0], p0[1], p0[2]);
            dst[2] = RgbToY(p1[0], p1[1], p1[2]);
            dst[3] = RgbToV(p0[0], p0[1], p0[2]);
        }
    }
}

// ── Standby frame ─────────────────────────────────────────────────────────────
// A minimal 5x7 bitmap font for rendering text into the standby frame.
static const BYTE kSBFont[][7] = {
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

static BYTE  g_standbyRgba[DEFAULT_WIDTH * DEFAULT_HEIGHT * 4];
static BYTE  g_standbyYuy2[DEFAULT_WIDTH * DEFAULT_HEIGHT * 2];
static bool  g_standbyReady = false;

// ── DLL logger (writes to %APPDATA%\peercam-desktop\vcam_filter.log) ──────────────
static HANDLE g_hLogFile = INVALID_HANDLE_VALUE;

static void DllLog(const char* fmt, ...) {
    if (g_hLogFile == INVALID_HANDLE_VALUE) return;
    char buf[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int hdrLen = wsprintfA(buf, "[%02d:%02d:%02d.%03d] [vcam_dll] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    int msgLen = wvsprintfA(buf + hdrLen, fmt, args);
    va_end(args);
    buf[hdrLen + msgLen] = '\n';
    DWORD written = 0;
    WriteFile(g_hLogFile, buf, (DWORD)(hdrLen + msgLen + 1), &written, nullptr);
}

static void OpenDllLog() {
    wchar_t appData[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    wchar_t logPath[MAX_PATH] = {};
    swprintf_s(logPath, L"%ls\\peercam-desktop\\vcam_filter.log", appData);
    g_hLogFile = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void SB_SetPixelRgba(BYTE* rgba, int W, int x, int y, BYTE r, BYTE g, BYTE b) {
    if (x < 0 || x >= W || y < 0 || y >= DEFAULT_HEIGHT) return;
    BYTE* p = rgba + ((y * W) + x) * 4;
    p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF;
}

static void SB_DrawChar(BYTE* rgba, int W, int ox, int oy, char ch, BYTE r, BYTE g, BYTE b) {
    if (ch < 32 || ch > 126) return;
    const BYTE* glyph = kSBFont[ch - 32];
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if ((glyph[row] & (0x10 >> col)) == 0) continue;
            SB_SetPixelRgba(rgba, W, ox + (col * 2),     oy + (row * 2),     r, g, b);
            SB_SetPixelRgba(rgba, W, ox + (col * 2) + 1, oy + (row * 2),     r, g, b);
            SB_SetPixelRgba(rgba, W, ox + (col * 2),     oy + (row * 2) + 1, r, g, b);
            SB_SetPixelRgba(rgba, W, ox + (col * 2) + 1, oy + (row * 2) + 1, r, g, b);
        }
    }
}

static void SB_DrawString(BYTE* rgba, int W, int x, int y, const char* text, BYTE r, BYTE g, BYTE b) {
    const int charW = 12; // 5 cols * 2 scale + 2 gap
    for (int i = 0; text[i]; i++)
        SB_DrawChar(rgba, W, x + (i * charW), y, text[i], r, g, b);
}

static void BuildStandbyFrame() {
    const int W = DEFAULT_WIDTH, H = DEFAULT_HEIGHT;
    BYTE* rgba = g_standbyRgba;

    // Dark background
    for (int i = 0; i < (W * H); i++) {
        rgba[(i*4)+0] = 0x1A; rgba[(i*4)+1] = 0x1A;
        rgba[(i*4)+2] = 0x2E; rgba[(i*4)+3] = 0xFF;
    }

    // Camera icon circle
    const int cx = W / 2, cy = (H / 2) - 30;
    const int outerR    = 28,  innerR    = 16;
    const int outer2    = outerR * outerR;
    const int inner2    = innerR * innerR;
    const int innerRing = (outerR - 3) * (outerR - 3);
    for (int y = cy - outerR - 2; y <= cy + outerR + 2; y++) {
        for (int x = cx - outerR - 2; x <= cx + outerR + 2; x++) {
            const int dx    = x - cx, dy = y - cy;
            const int dist2 = (dx * dx) + (dy * dy);
            if ((dist2 <= outer2) && (dist2 >= innerRing))
                SB_SetPixelRgba(rgba, W, x, y, 0x63, 0x63, 0xD8);
            if (dist2 <= inner2)
                SB_SetPixelRgba(rgba, W, x, y, 0x4F, 0x46, 0xE5);
        }
    }

    // Text
    const char* line1 = "Waiting for";
    const char* line2 = "connection...";
    const char* brand = "PeerCam";
    const int charW = 12, charH = 16;
    const int textY = cy + outerR + 16;
    SB_DrawString(rgba, W, (W - ((int)lstrlenA(line1) * charW)) / 2, textY,           line1, 0xA1, 0xA1, 0xAA);
    SB_DrawString(rgba, W, (W - ((int)lstrlenA(line2) * charW)) / 2, textY + charH + 4, line2, 0xA1, 0xA1, 0xAA);
    SB_DrawString(rgba, W, (W - ((int)lstrlenA(brand) * charW)) / 2, H - charH - 16,  brand, 0x63, 0x63, 0xD8);

    RgbaToYuy2(rgba, g_standbyYuy2, W, H);
    g_standbyReady = true;
}

static long FrameBytes(const VideoCapability& cap) {
    return cap.width * cap.height * 2;
}

static HRESULT BuildVideoMediaType(const VideoCapability& cap, CMediaType* pmt) {
    VIDEOINFOHEADER* pvi = reinterpret_cast<VIDEOINFOHEADER*>(
        pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
    if (!pvi) {
        return E_OUTOFMEMORY;
    }
    ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));
    SetRect(&pvi->rcSource, 0, 0, cap.width, cap.height);
    SetRect(&pvi->rcTarget, 0, 0, cap.width, cap.height);
    pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pvi->bmiHeader.biWidth = cap.width;
    pvi->bmiHeader.biHeight = -cap.height;
    pvi->bmiHeader.biPlanes = 1;
    pvi->bmiHeader.biBitCount = 16;
    pvi->bmiHeader.biCompression = MAKEFOURCC('Y','U','Y','2');
    pvi->bmiHeader.biSizeImage = FrameBytes(cap);
    pvi->AvgTimePerFrame = UNITS / cap.fps;
    pmt->SetType(&MEDIATYPE_Video);
    pmt->SetSubtype(&MEDIASUBTYPE_YUY2);
    pmt->SetFormatType(&FORMAT_VideoInfo);
    pmt->SetTemporalCompression(FALSE);
    pmt->SetSampleSize(FrameBytes(cap));
    return S_OK;
}

static void BuildVideoStreamCaps(const VideoCapability& cap, VIDEO_STREAM_CONFIG_CAPS* caps) {
    ZeroMemory(caps, sizeof(*caps));
    caps->guid = FORMAT_VideoInfo;
    caps->VideoStandard = AnalogVideo_None;
    caps->InputSize.cx = cap.width;
    caps->InputSize.cy = cap.height;
    caps->MinCroppingSize.cx = cap.width;
    caps->MinCroppingSize.cy = cap.height;
    caps->MaxCroppingSize.cx = cap.width;
    caps->MaxCroppingSize.cy = cap.height;
    caps->CropGranularityX = 1;
    caps->CropGranularityY = 1;
    caps->CropAlignX = 1;
    caps->CropAlignY = 1;
    caps->MinOutputSize.cx = cap.width;
    caps->MinOutputSize.cy = cap.height;
    caps->MaxOutputSize.cx = cap.width;
    caps->MaxOutputSize.cy = cap.height;
    caps->OutputGranularityX = 1;
    caps->OutputGranularityY = 1;
    caps->StretchTapsX = 0;
    caps->StretchTapsY = 0;
    caps->ShrinkTapsX = 0;
    caps->ShrinkTapsY = 0;
    caps->MinFrameInterval = UNITS / cap.fps;
    caps->MaxFrameInterval = UNITS / cap.fps;
    caps->MinBitsPerSecond = cap.width * cap.height * 16 * cap.fps;
    caps->MaxBitsPerSecond = caps->MinBitsPerSecond;
}

static bool ParseCapabilityFromMediaType(
    const AM_MEDIA_TYPE* pmt,
    size_t* capIndex,
    VideoCapability* parsedCap)
{
    if (!pmt || pmt->majortype != MEDIATYPE_Video || pmt->subtype != MEDIASUBTYPE_YUY2) {
        return false;
    }
    if (pmt->formattype != FORMAT_VideoInfo || !pmt->pbFormat || pmt->cbFormat < sizeof(VIDEOINFOHEADER)) {
        return false;
    }

    const VIDEOINFOHEADER* pvi = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
    const LONG width = pvi->bmiHeader.biWidth;
    const LONG height = std::abs(pvi->bmiHeader.biHeight);
    LONG fps = DEFAULT_FPS;
    if (pvi->AvgTimePerFrame > 0) {
        fps = static_cast<LONG>((UNITS + (pvi->AvgTimePerFrame / 2)) / pvi->AvgTimePerFrame);
    }

    for (size_t i = 0; i < _countof(kCapabilities); ++i) {
        const VideoCapability& cap = kCapabilities[i];
        if (cap.width == width && cap.height == height && cap.fps == fps) {
            if (capIndex) {
                *capIndex = i;
            }
            if (parsedCap) {
                *parsedCap = cap;
            }
            return true;
        }
    }

    return false;
}

// ── Stream ────────────────────────────────────────────────────────────────────
class CPeerCamStream : public CSourceStream, public IAMStreamConfig, public IKsPropertySet {
public:
    DECLARE_IUNKNOWN

    CPeerCamStream(HRESULT* phr, CSource* pParent, LPCWSTR pName)
        : CSourceStream(pName, phr, pParent, L"Output")
        , m_hMapFile(nullptr), m_pShm(nullptr)
        , m_hEvent(nullptr), m_hMutex(nullptr)
        , m_lastFrameCount(0)
        , m_lastFrameTick(0)
        , m_lastLiveFrameTick(0)
        , m_width(DEFAULT_WIDTH), m_height(DEFAULT_HEIGHT), m_fps(DEFAULT_FPS)
        , m_shmWasOpen(false), m_fillCount(0)
        , m_lastRenderMode(-1)
    {
        if (!g_standbyReady) BuildStandbyFrame();
        TryOpenSharedMemory();
        DllLog("stream_created shm=%s pid=%lu", m_pShm ? "open" : "none", GetCurrentProcessId());
    }

    ~CPeerCamStream() {
        CloseSharedMemory();
    }

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IAMStreamConfig) {
            return GetInterface(static_cast<IAMStreamConfig*>(this), ppv);
        }
        if (riid == IID_IKsPropertySet) {
            return GetInterface(static_cast<IKsPropertySet*>(this), ppv);
        }
        return CSourceStream::NonDelegatingQueryInterface(riid, ppv);
    }

    HRESULT OnThreadCreate() override {
        m_rtSampleTime = 0;
        m_lastFrameTick = 0;
        return S_OK;
    }

    HRESULT CheckMediaType(const CMediaType* pmt) override {
        return ParseCapabilityFromMediaType(pmt, nullptr, nullptr) ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
    }

    HRESULT GetMediaType(int iPosition, CMediaType* pmt) override {
        if (iPosition < 0) {
            return E_INVALIDARG;
        }
        if (static_cast<size_t>(iPosition) >= _countof(kCapabilities)) {
            return VFW_S_NO_MORE_ITEMS;
        }
        return BuildVideoMediaType(kCapabilities[iPosition], pmt);
    }

    HRESULT GetMediaType(CMediaType* pmt) override {
        CAutoLock lock(m_pFilter->pStateLock());
        DllLog("GetMediaType negotiating %ldx%ld YUY2 @%ldfps pid=%lu",
            m_width, m_height, m_fps, GetCurrentProcessId());
        return BuildVideoMediaType({ m_width, m_height, m_fps }, pmt);
    }

    STDMETHODIMP SetFormat(AM_MEDIA_TYPE* pmt) override {
        if (!pmt) {
            return E_POINTER;
        }

        VideoCapability cap = {};
        if (!ParseCapabilityFromMediaType(pmt, nullptr, &cap)) {
            return VFW_E_INVALIDMEDIATYPE;
        }

        CAutoLock lock(m_pFilter->pStateLock());
        if (IsConnected()) {
            return VFW_E_ALREADY_CONNECTED;
        }
        m_width = cap.width;
        m_height = cap.height;
        m_fps = cap.fps;
        DllLog("SetFormat %ldx%ld @%ldfps pid=%lu",
            m_width, m_height, m_fps, GetCurrentProcessId());
        return S_OK;
    }

    STDMETHODIMP GetFormat(AM_MEDIA_TYPE** ppmt) override {
        if (!ppmt) {
            return E_POINTER;
        }
        CMediaType mt;
        HRESULT hr = GetMediaType(&mt);
        if (FAILED(hr)) {
            return hr;
        }
        *ppmt = CreateMediaType(&mt);
        return *ppmt ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetNumberOfCapabilities(int* piCount, int* piSize) override {
        if (!piCount || !piSize) {
            return E_POINTER;
        }
        *piCount = static_cast<int>(_countof(kCapabilities));
        *piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
        return S_OK;
    }

    STDMETHODIMP GetStreamCaps(int iIndex, AM_MEDIA_TYPE** ppmt, BYTE* pSCC) override {
        if (!ppmt || !pSCC) {
            return E_POINTER;
        }
        if (iIndex < 0 || static_cast<size_t>(iIndex) >= _countof(kCapabilities)) {
            return S_FALSE;
        }

        CMediaType mt;
        HRESULT hr = BuildVideoMediaType(kCapabilities[iIndex], &mt);
        if (FAILED(hr)) {
            return hr;
        }

        *ppmt = CreateMediaType(&mt);
        if (!*ppmt) {
            return E_OUTOFMEMORY;
        }

        BuildVideoStreamCaps(kCapabilities[iIndex], reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(pSCC));
        return S_OK;
    }

    STDMETHODIMP Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) override {
        return E_NOTIMPL;
    }

    STDMETHODIMP Get(
        REFGUID guidPropSet,
        DWORD dwPropID,
        LPVOID,
        DWORD,
        LPVOID pPropData,
        DWORD cbPropData,
        DWORD* pcbReturned) override
    {
        if (guidPropSet != AMPROPSETID_Pin || dwPropID != AMPROPERTY_PIN_CATEGORY) {
            return E_PROP_ID_UNSUPPORTED;
        }
        if (pcbReturned) {
            *pcbReturned = sizeof(GUID);
        }
        if (!pPropData) {
            return E_POINTER;
        }
        if (cbPropData < sizeof(GUID)) {
            return E_UNEXPECTED;
        }
        *reinterpret_cast<GUID*>(pPropData) = PIN_CATEGORY_CAPTURE;
        return S_OK;
    }

    STDMETHODIMP QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD* pTypeSupport) override {
        if (!pTypeSupport) {
            return E_POINTER;
        }
        if (guidPropSet != AMPROPSETID_Pin || dwPropID != AMPROPERTY_PIN_CATEGORY) {
            return E_PROP_ID_UNSUPPORTED;
        }
        *pTypeSupport = 1;
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pReq) override {
        ALLOCATOR_PROPERTIES actual = {};
        pReq->cBuffers = 2;
        pReq->cbBuffer = FrameBytes({ m_width, m_height, m_fps });
        HRESULT hr = pAlloc->SetProperties(pReq, &actual);
        DllLog("DecideBufferSize hr=0x%08X bufs=%d bufSize=%d pid=%lu",
            (unsigned)hr, actual.cBuffers, actual.cbBuffer, GetCurrentProcessId());
        return hr;
    }

    HRESULT FillBuffer(IMediaSample* pSample) override {
        BYTE* pData = nullptr;
        pSample->GetPointer(&pData);
        const long cbData = pSample->GetSize();
        const VideoCapability cap = { m_width, m_height, m_fps };
        const long actualBytes = FrameBytes(cap);

        // Self-throttle to DEFAULT_FPS using wall clock — never Sleep() on the DS thread
        const DWORD frameMs = std::max<DWORD>(1, 1000 / static_cast<DWORD>(cap.fps));
        const DWORD now = GetTickCount();
        const DWORD elapsed = now - m_lastFrameTick;
        if (m_lastFrameTick != 0 && elapsed < frameMs)
            Sleep(frameMs - elapsed);
        m_lastFrameTick = GetTickCount();

        // Detect stale SHM: if the named mapping no longer exists, release our view
        if (m_pShm) {
            HANDLE probe = OpenFileMappingA(FILE_MAP_READ, FALSE, PEERCAM_SHM_NAME);
            if (!probe) {
                // Electron closed — release stale handles
                CloseSharedMemory();
            } else {
                CloseHandle(probe);
            }
        }

        // Try to open SHM if not yet open
        if (!m_pShm) {
            TryOpenSharedMemory();
        }

        if (m_pShm) {
            if (!m_shmWasOpen) {
                m_shmWasOpen = true;
                DllLog("shm_connected Electron_running pid=%lu", GetCurrentProcessId());
            }
            if (m_hEvent) {
                WaitForSingleObject(m_hEvent, frameMs);
            }
            bool wroteLiveFrame = false;
            const bool locked = !m_hMutex || WaitForSingleObject(m_hMutex, 16) == WAIT_OBJECT_0;
            if (locked) {
                const PeerCamShmHeader* hdr = reinterpret_cast<const PeerCamShmHeader*>(m_pShm);
                const DWORD w = hdr->width;
                const DWORD h = hdr->height;
                const DWORD frameCount = hdr->frameCount;
                if (w > 0 && h > 0 && w <= PEERCAM_MAX_WIDTH && h <= PEERCAM_MAX_HEIGHT) {
                    if (frameCount != m_lastFrameCount) {
                        m_lastFrameCount = frameCount;
                        m_lastLiveFrameTick = GetTickCount();
                    }
                    if (m_lastLiveFrameTick != 0 && (GetTickCount() - m_lastLiveFrameTick) <= FRAME_STALE_TIMEOUT_MS) {
                        if (actualBytes <= cbData) {
                            const BYTE* rgba = PeerCamPixelData(m_pShm);
                            ScaleRgbaToYuy2Letterboxed(
                                rgba, static_cast<int>(w), static_cast<int>(h),
                                pData, cap.width, cap.height, 0x1A, 0x1A, 0x2E);
                            wroteLiveFrame = true;
                            LogRenderMode(2, "live_frame");
                        }
                    } else {
                        LogRenderMode(1, "stale_input");
                    }
                } else {
                    LogRenderMode(1, "invalid_input");
                }
                if (m_hMutex) {
                    ReleaseMutex(m_hMutex);
                }
            } else {
                LogRenderMode(1, "mutex_busy");
            }
            if (wroteLiveFrame) {
                m_fillCount++;
                REFERENCE_TIME rtStart = m_rtSampleTime;
                m_rtSampleTime += UNITS / cap.fps;
                pSample->SetTime(&rtStart, &m_rtSampleTime);
                pSample->SetActualDataLength(actualBytes);
                pSample->SetSyncPoint(TRUE);
                return S_OK;
            }
        } else {
            if (m_shmWasOpen) {
                m_shmWasOpen = false;
                DllLog("shm_disconnected Electron_closed pid=%lu", GetCurrentProcessId());
            }
            LogRenderMode(0, "no_shm");
            if (m_fillCount == 0)
                DllLog("standby_mode no_Electron pid=%lu", GetCurrentProcessId());
            if (g_standbyReady && actualBytes <= cbData) {
                if (cap.width == DEFAULT_WIDTH && cap.height == DEFAULT_HEIGHT) {
                    CopyMemory(pData, g_standbyYuy2, actualBytes);
                } else {
                    ScaleRgbaToYuy2Letterboxed(
                        g_standbyRgba, DEFAULT_WIDTH, DEFAULT_HEIGHT,
                        pData, cap.width, cap.height, 0x1A, 0x1A, 0x2E);
                }
                LogRenderMode(1, "standby");
            } else {
                ZeroMemory(pData, cbData);
            }
        }
        if (m_pShm) {
            if (g_standbyReady && actualBytes <= cbData) {
                if (cap.width == DEFAULT_WIDTH && cap.height == DEFAULT_HEIGHT) {
                    CopyMemory(pData, g_standbyYuy2, actualBytes);
                } else {
                    ScaleRgbaToYuy2Letterboxed(
                        g_standbyRgba, DEFAULT_WIDTH, DEFAULT_HEIGHT,
                        pData, cap.width, cap.height, 0x1A, 0x1A, 0x2E);
                }
                LogRenderMode(1, "standby");
            } else {
                ZeroMemory(pData, cbData);
            }
        }
        m_fillCount++;

        REFERENCE_TIME rtStart = m_rtSampleTime;
        m_rtSampleTime += UNITS / cap.fps;
        pSample->SetTime(&rtStart, &m_rtSampleTime);
        pSample->SetActualDataLength(actualBytes);
        pSample->SetSyncPoint(TRUE);
        return S_OK;
    }

private:
    void TryOpenSharedMemory() {
        if (!m_hMapFile) {
            m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, PEERCAM_SHM_NAME);
        }
        if (m_hMapFile && !m_pShm) {
            m_pShm = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, PEERCAM_SHM_SIZE);
        }
        if (!m_hEvent) {
            m_hEvent = OpenEventA(SYNCHRONIZE, FALSE, PEERCAM_EVENT_NAME);
        }
        if (!m_hMutex) {
            m_hMutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, PEERCAM_MUTEX_NAME);
        }
    }

    void CloseSharedMemory() {
        if (m_pShm) {
            UnmapViewOfFile(m_pShm);
            m_pShm = nullptr;
        }
        if (m_hMapFile) {
            CloseHandle(m_hMapFile);
            m_hMapFile = nullptr;
        }
        if (m_hEvent) {
            CloseHandle(m_hEvent);
            m_hEvent = nullptr;
        }
        if (m_hMutex) {
            CloseHandle(m_hMutex);
            m_hMutex = nullptr;
        }
    }

    void LogRenderMode(int mode, const char* reason) {
        if (m_lastRenderMode == mode) {
            return;
        }
        m_lastRenderMode = mode;
        DllLog("render_mode %s pid=%lu", reason, GetCurrentProcessId());
    }

    HANDLE m_hMapFile, m_hEvent, m_hMutex;
    void*  m_pShm;
    DWORD  m_lastFrameCount;
    DWORD  m_lastFrameTick;
    DWORD  m_lastLiveFrameTick;
    LONG   m_width, m_height, m_fps;
    bool   m_shmWasOpen;
    DWORD  m_fillCount;
    int    m_lastRenderMode;
    REFERENCE_TIME m_rtSampleTime = 0;
};

// ── Filter ────────────────────────────────────────────────────────────────────
class CPeerCamFilter : public CSource {
public:
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
        return new CPeerCamFilter(pUnk, phr);
    }
    CPeerCamFilter(LPUNKNOWN pUnk, HRESULT* phr)
        : CSource(FILTER_NAME, pUnk, CLSID_PeerCamVCam)
    {
        new CPeerCamStream(phr, this, L"PeerCam");
    }
};

// ── Registration ──────────────────────────────────────────────────────────────
static const AMOVIESETUP_MEDIATYPE sudOpPinTypes = {
    &MEDIATYPE_Video, &MEDIASUBTYPE_YUY2
};
static const AMOVIESETUP_PIN sudOpPin = {
    L"Output", FALSE, TRUE, FALSE, FALSE,
    &CLSID_NULL, nullptr, 1, &sudOpPinTypes
};
static const AMOVIESETUP_FILTER sudFilter = {
    &CLSID_PeerCamVCam, FILTER_NAME, MERIT_DO_NOT_USE, 1, &sudOpPin
};

CFactoryTemplate g_Templates[] = {
    { FILTER_NAME, &CLSID_PeerCamVCam, CPeerCamFilter::CreateInstance, nullptr, &sudFilter }
};
int g_cTemplates = 1;
static HINSTANCE g_hInstance = nullptr;

static DWORD WideStringBytes(const wchar_t* text)
{
    return static_cast<DWORD>((wcslen(text) + 1) * sizeof(wchar_t));
}

static HRESULT RegisterComClass()
{
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_hInstance, modulePath, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    wchar_t clsidString[64] = {};
    StringFromGUID2(CLSID_PeerCamVCam, clsidString, _countof(clsidString));

    wchar_t classKeyPath[128] = {};
    swprintf_s(classKeyPath, L"Software\\Classes\\CLSID\\%ls", clsidString);

    HKEY classKey = nullptr;
    HKEY serverKey = nullptr;
    LONG status = RegCreateKeyExW(HKEY_CURRENT_USER, classKeyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &classKey, nullptr);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }

    status = RegSetValueExW(classKey, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(FILTER_NAME), WideStringBytes(FILTER_NAME));
    if (status == ERROR_SUCCESS) {
        status = RegCreateKeyExW(classKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &serverKey, nullptr);
    }
    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(serverKey, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(modulePath), WideStringBytes(modulePath));
    }
    if (status == ERROR_SUCCESS) {
        static const wchar_t threadingModel[] = L"Both";
        status = RegSetValueExW(serverKey, L"ThreadingModel", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(threadingModel), WideStringBytes(threadingModel));
    }

    if (serverKey) {
        RegCloseKey(serverKey);
    }
    if (classKey) {
        RegCloseKey(classKey);
    }

    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

static HRESULT UnregisterComClass()
{
    wchar_t clsidString[64] = {};
    StringFromGUID2(CLSID_PeerCamVCam, clsidString, _countof(clsidString));

    wchar_t classKeyPath[128] = {};
    swprintf_s(classKeyPath, L"Software\\Classes\\CLSID\\%ls", clsidString);

    const LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, classKeyPath);
    if (status == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

static HRESULT RegisterVideoInputCategory(BOOL reg)
{
    wchar_t clsidString[64] = {};
    StringFromGUID2(CLSID_PeerCamVCam, clsidString, _countof(clsidString));

    wchar_t keyPath[192] = {};
    swprintf_s(
        keyPath,
        L"Software\\Classes\\CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance\\%ls",
        clsidString
    );

    if (!reg) {
        const LONG status = RegDeleteTreeW(HKEY_CURRENT_USER, keyPath);
        if (status == ERROR_FILE_NOT_FOUND) {
            return S_OK;
        }
        return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
    }

    static const BYTE filterData[] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x70, 0x69, 0x33, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x30, 0x74, 0x79, 0x33, 0x00, 0x00, 0x00, 0x00,
        0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00,
        0x76, 0x69, 0x64, 0x73, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71,
        0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
    };

    HKEY key = nullptr;
    const LONG createStatus = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (createStatus != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(createStatus);
    }

    LONG status = RegSetValueExW(
        key,
        L"FriendlyName",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(FILTER_NAME),
        WideStringBytes(FILTER_NAME)
    );
    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(
            key,
            L"CLSID",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(clsidString),
            WideStringBytes(clsidString)
        );
    }
    if (status == ERROR_SUCCESS) {
        status = RegSetValueExW(
            key,
            L"FilterData",
            0,
            REG_BINARY,
            filterData,
            sizeof(filterData)
        );
    }

    RegCloseKey(key);
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

STDAPI DllRegisterServer()
{
    HRESULT hr = RegisterComClass();
    if (FAILED(hr)) {
        return hr;
    }

    hr = RegisterVideoInputCategory(TRUE);
    if (FAILED(hr)) {
        RegisterVideoInputCategory(FALSE);
        UnregisterComClass();
    }

    return hr;
}

STDAPI DllUnregisterServer()
{
    RegisterVideoInputCategory(FALSE);
    return UnregisterComClass();
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
BOOL WINAPI DllMain(HINSTANCE hDll, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hDll;
        OpenDllLog();
        DllLog("DLL_PROCESS_ATTACH pid=%lu exe=%ls", GetCurrentProcessId(),
            []() { static wchar_t p[MAX_PATH]={}; GetModuleFileNameW(nullptr,p,MAX_PATH); return p; }());
        // Self-repair: if our Video Input Device category entry is missing, re-register.
        // This handles upgrades or manual registry cleanups that leave the DLL loaded
        // but unregistered, causing Chrome/Zoom to not see the device.
        wchar_t clsidString[64] = {};
        StringFromGUID2(CLSID_PeerCamVCam, clsidString, _countof(clsidString));
        wchar_t checkPath[192] = {};
        swprintf_s(checkPath,
            L"Software\\Classes\\CLSID\\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\\Instance\\%ls",
            clsidString);
        HKEY hk = nullptr;
        const bool missing = (RegOpenKeyExW(HKEY_CURRENT_USER, checkPath, 0, KEY_READ, &hk) != ERROR_SUCCESS);
        if (hk) RegCloseKey(hk);
        if (missing) {
            RegisterComClass();
            RegisterVideoInputCategory(TRUE);
        }
    }
    return DllEntryPoint(hDll, dwReason, lpReserved);
}
