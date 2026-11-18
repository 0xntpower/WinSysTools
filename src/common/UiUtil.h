// UiUtil.h -- small shared UI helpers for the recreated tools.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

// Strip the visual style from a toolbar. A v6-themed flat toolbar on a dark-mode
// system paints a dark band that SetPreferredAppMode(ForceLight) does not reach.
// With the theme removed the toolbar draws classic, so the frame's button-face
// background shows through the masked strip's transparent pixels and the bar reads
// light, matching the original tools. Call once, right after the toolbar is made.
inline void PinToolbarLight(HWND hToolbar)
{
    ::SetWindowTheme(hToolbar, L"", L"");
}

// Fill a frame's whole client area with the button-face colour, so the transparent
// band left by a de-themed flat toolbar reads light. The ListView and any panels
// paint over their own areas, so only the toolbar gap shows this. Call from the
// frame's WM_ERASEBKGND handler and return its value.
inline LRESULT FillFrameBtnFace(HWND hWnd, HDC hdc)
{
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    ::FillRect(hdc, &rc, ::GetSysColorBrush(COLOR_BTNFACE));
    return 1;
}

// Pin the process to light mode so the themed common controls match the original
// tools, which do not follow the system dark theme. Without this, a v6
// themed toolbar on a dark-mode system paints a dark background. Call once at the
// top of WinMain, before InitCommonControls. A no-op on Windows without the API.
inline void PinToLightMode()
{
    if (HMODULE ux = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
    {
        using SetPreferredAppModeFn = int (WINAPI*)(int);
        if (auto setMode = reinterpret_cast<SetPreferredAppModeFn>(::GetProcAddress(ux, MAKEINTRESOURCEA(135))))
            setMode(3); // PreferredAppMode::ForceLight
        // Keep uxtheme loaded for the life of the process.
    }
}

// Load an old-style toolbar strip bitmap into a masked imagelist, keying
// the bitmap's own background corner colour transparent. The 4-bit strips use a
// light-gray background, the newer 24-bit strips use black, so read the actual
// corner pixel rather than assuming one. A 24-bit image list (ILC_COLOR24) is
// used rather than 32-bit, because a 32-bit list carries an alpha channel that
// ImageList_AddMasked leaves opaque on the keyed pixels, so the system blends
// instead of honouring the mask and the background shows as a box behind the
// icons. With ILC_COLOR24 there is no alpha and the mask renders transparent.
// Returns a cx-high imagelist, or null.
inline HIMAGELIST LoadToolbarImageList(HINSTANCE hInst, UINT bitmapId, int cx = 16)
{
    HBITMAP hbm = static_cast<HBITMAP>(::LoadImageW(hInst, MAKEINTRESOURCEW(bitmapId),
        IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    if (!hbm)
        return nullptr;

    BITMAP bm{};
    ::GetObject(hbm, sizeof(bm), &bm);

    HDC dc = ::CreateCompatibleDC(nullptr);
    HGDIOBJ prev = ::SelectObject(dc, hbm);
    COLORREF key = ::GetPixel(dc, 0, bm.bmHeight - 1); // background corner pixel
    ::SelectObject(dc, prev);
    ::DeleteDC(dc);

    int count = (cx > 0) ? (bm.bmWidth / cx) : 1;
    HIMAGELIST il = ::ImageList_Create(cx, bm.bmHeight, ILC_COLOR24 | ILC_MASK,
                                       count > 0 ? count : 1, 0);
    ::ImageList_AddMasked(il, hbm, key);
    ::DeleteObject(hbm);
    return il;
}
