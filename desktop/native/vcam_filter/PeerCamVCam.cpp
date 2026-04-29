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
    int pixels = width * height;
    for (int i = 0; i < pixels; i += 2) {
        const BYTE* p0 = rgba + i * 4;
        const BYTE* p1 = rgba + (i + 1) * 4;
        BYTE y0 = (BYTE)(0.299f*p0[0] + 0.587f*p0[1] + 0.114f*p0[2]);
        BYTE y1 = (BYTE)(0.299f*p1[0] + 0.587f*p1[1] + 0.114f*p1[2]);
        BYTE u  = (BYTE)(128 - 0.168736f*p0[0] - 0.331264f*p0[1] + 0.5f*p0[2]);
        BYTE v  = (BYTE)(128 + 0.5f*p0[0] - 0.418688f*p0[1] - 0.081312f*p0[2]);
        *yuy2++ = y0; *yuy2++ = u; *yuy2++ = y1; *yuy2++ = v;
    }
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

        // Wait up to 50ms for a new frame, then deliver last frame (keeps stream alive)
        if (m_hEvent) WaitForSingleObject(m_hEvent, 50);

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
            if (m_hMutex) WaitForSingleObject(m_hMutex, 16);
            const PeerCamShmHeader* hdr = reinterpret_cast<const PeerCamShmHeader*>(m_pShm);
            DWORD w = hdr->width, h = hdr->height;
            if (w > 0 && h > 0 && w <= PEERCAM_MAX_WIDTH && h <= PEERCAM_MAX_HEIGHT) {
                // Only render if dimensions match what we negotiated — avoids stride corruption
                // If they differ, output black and let the next GetMediaType call pick up new dims
                if ((int)w == m_width && (int)h == m_height) {
                    const BYTE* rgba = PeerCamPixelData(const_cast<void*>(m_pShm));
                    size_t needed = (size_t)w * h * 2;
                    if ((long)needed <= cbData)
                        RgbaToYuy2(rgba, pData, w, h);
                } else {
                    // Resolution changed — output black, update dims for next negotiation
                    m_width  = (int)w;
                    m_height = (int)h;
                    ZeroMemory(pData, cbData);
                }
            }
            if (m_hMutex) ReleaseMutex(m_hMutex);
        } else {
            // No PeerCam connection — output black frame
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
