// notify_devices.cpp
// Broadcasts WM_DEVICECHANGE / DBT_DEVNODES_CHANGED to all top-level windows
// so Chrome, Zoom, Teams etc. re-enumerate camera devices immediately.
// Build: cl /O1 /EHsc /DWIN32 notify_devices.cpp user32.lib /Fe:notify_devices.exe /link /SUBSYSTEM:CONSOLE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main() {
    ULONG_PTR result = 0;
    SendMessageTimeout(
        HWND_BROADCAST,
        WM_DEVICECHANGE,
        (WPARAM)0x0007, // DBT_DEVNODES_CHANGED
        0,
        SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
        5000,
        &result
    );
    return 0;
}
