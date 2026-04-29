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
#include <windows.h>
#include <dshow.h>
#include <streams.h>
#include <initguid.h>
#include <uuids.h>
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

// ── RGBA → YUY2 conversion ────────────────────────────────────────────────────
static void RgbaToYuy2(const BYTE* rgba, BYTE* yuy2, int width, int height) {
    const int pixels = width * height;
    for (int i = 0; i < pixels; i += 2) {
        const BYTE* p0 = rgba + (i * 4);
        const BYTE* p1 = rgba + ((i + 1) * 4);
        const BYTE y0 = (BYTE)((0.299f * p0[0]) + (0.587f * p0[1]) + (0.114f * p0[2]));
        const BYTE y1 = (BYTE)((0.299f * p1[0]) + (0.587f * p1[1]) + (0.114f * p1[2]));
        const BYTE u  = (BYTE)(128.0f - (0.168736f * p0[0]) - (0.331264f * p0[1]) + (0.5f * p0[2]));
        const BYTE v  = (BYTE)(128.0f + (0.5f * p0[0]) - (0.418688f * p0[1]) - (0.081312f * p0[2]));
        *yuy2++ = y0; *yuy2++ = u; *yuy2++ = y1; *yuy2++ = v;
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

static BYTE  g_standbyYuy2[DEFAULT_WIDTH * DEFAULT_HEIGHT * 2];
static bool  g_standbyReady = false;

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
    static BYTE rgba[DEFAULT_WIDTH * DEFAULT_HEIGHT * 4];

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

// ── Stream ────────────────────────────────────────────────────────────────────
class CPeerCamStream : public CSourceStream {
public:
    CPeerCamStream(HRESULT* phr, CSource* pParent, LPCWSTR pName)
        : CSourceStream(pName, phr, pParent, L"Output")
        , m_hMapFile(nullptr), m_pShm(nullptr)
        , m_hEvent(nullptr), m_hMutex(nullptr)
        , m_lastFrameCount(0)
        , m_width(DEFAULT_WIDTH), m_height(DEFAULT_HEIGHT)
    {
        if (!g_standbyReady) BuildStandbyFrame();
        // Open shared memory (created by vcam_win.cc when PeerCam connects)
        m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, PEERCAM_SHM_NAME);
        if (m_hMapFile)
            m_pShm = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, PEERCAM_SHM_SIZE);
        m_hEvent = OpenEventA(SYNCHRONIZE, FALSE, PEERCAM_EVENT_NAME);
        m_hMutex = OpenMutexA(SYNCHRONIZE, FALSE, PEERCAM_MUTEX_NAME);
    }

    ~CPeerCamStream() {
        if (m_pShm)     UnmapViewOfFile(m_pShm);
        if (m_hMapFile) CloseHandle(m_hMapFile);
        if (m_hEvent)   CloseHandle(m_hEvent);
        if (m_hMutex)   CloseHandle(m_hMutex);
    }

    HRESULT GetMediaType(CMediaType* pmt) override {
        CAutoLock lock(m_pFilter->pStateLock());
        // Read actual dimensions from shared memory if available
        if (m_pShm) {
            const PeerCamShmHeader* hdr = reinterpret_cast<const PeerCamShmHeader*>(m_pShm);
            if (hdr->width > 0 && hdr->width <= PEERCAM_MAX_WIDTH &&
                hdr->height > 0 && hdr->height <= PEERCAM_MAX_HEIGHT) {
                m_width  = (int)hdr->width;
                m_height = (int)hdr->height;
            }
        }
        VIDEOINFO* pvi = (VIDEOINFO*)pmt->AllocFormatBuffer(sizeof(VIDEOINFO));
        if (!pvi) return E_OUTOFMEMORY;
        ZeroMemory(pvi, sizeof(VIDEOINFO));
        pvi->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        pvi->bmiHeader.biWidth       = m_width;
        pvi->bmiHeader.biHeight      = -m_height;
        pvi->bmiHeader.biPlanes      = 1;
        pvi->bmiHeader.biBitCount    = 16;
        pvi->bmiHeader.biCompression = MAKEFOURCC('Y','U','Y','2');
        pvi->bmiHeader.biSizeImage   = m_width * m_height * 2;
        pvi->AvgTimePerFrame         = UNITS / DEFAULT_FPS;
        pmt->SetType(&MEDIATYPE_Video);
        pmt->SetFormatType(&FORMAT_VideoInfo);
        pmt->SetTemporalCompression(FALSE);
        pmt->SetSubtype(&MEDIASUBTYPE_YUY2);
        pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pReq) override {
        ALLOCATOR_PROPERTIES actual;
        pReq->cBuffers = 2;
        // Allocate for max resolution so resolution changes never overflow
        pReq->cbBuffer = PEERCAM_MAX_WIDTH * PEERCAM_MAX_HEIGHT * 2;
        return pAlloc->SetProperties(pReq, &actual);
    }

    HRESULT FillBuffer(IMediaSample* pSample) override {
        BYTE* pData = nullptr;
        pSample->GetPointer(&pData);
        long cbData = pSample->GetSize();

        // Try to open shared memory if not yet open
        if (!m_pShm) {
            m_hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, PEERCAM_SHM_NAME);
            if (m_hMapFile)
                m_pShm = MapViewOfFile(m_hMapFile, FILE_MAP_READ, 0, 0, PEERCAM_SHM_SIZE);
            if (!m_hEvent)
                m_hEvent = OpenEventA(SYNCHRONIZE, FALSE, PEERCAM_EVENT_NAME);
            if (!m_hMutex)
                m_hMutex = OpenMutexA(SYNCHRONIZE, FALSE, PEERCAM_MUTEX_NAME);
        }

        if (m_pShm) {
            // Wait up to 50ms for a new frame signal
            if (m_hEvent) WaitForSingleObject(m_hEvent, 50);
            if (m_hMutex) WaitForSingleObject(m_hMutex, 16);
            const PeerCamShmHeader* hdr = reinterpret_cast<const PeerCamShmHeader*>(m_pShm);
            const DWORD w = hdr->width, h = hdr->height;
            if (w > 0 && h > 0 && w <= PEERCAM_MAX_WIDTH && h <= PEERCAM_MAX_HEIGHT) {
                if ((int)w == m_width && (int)h == m_height) {
                    const BYTE* rgba = PeerCamPixelData(const_cast<void*>(m_pShm));
                    const size_t needed = (size_t)w * h * 2;
                    if ((long)needed <= cbData)
                        RgbaToYuy2(rgba, pData, w, h);
                } else {
                    m_width  = (int)w;
                    m_height = (int)h;
                    ZeroMemory(pData, cbData);
                }
            }
            if (m_hMutex) ReleaseMutex(m_hMutex);
        } else {
            // Electron app not running — deliver standby frame at 30fps
            Sleep(1000 / DEFAULT_FPS);
            const size_t standbyBytes = (size_t)DEFAULT_WIDTH * DEFAULT_HEIGHT * 2;
            if (g_standbyReady && (long)standbyBytes <= cbData)
                CopyMemory(pData, g_standbyYuy2, standbyBytes);
            else
                ZeroMemory(pData, cbData);
        }

        REFERENCE_TIME rtStart = m_rtSampleTime;
        m_rtSampleTime += UNITS / DEFAULT_FPS;
        pSample->SetTime(&rtStart, &m_rtSampleTime);
        pSample->SetSyncPoint(TRUE);
        return S_OK;
    }

private:
    HANDLE m_hMapFile, m_hEvent, m_hMutex;
    void*  m_pShm;
    DWORD  m_lastFrameCount;
    int    m_width, m_height;
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
    }
    return DllEntryPoint(hDll, dwReason, lpReserved);
}
