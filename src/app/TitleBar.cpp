// Windows-only: make the native title bar dark (kept in its own file because of the windows.h macros)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace al
{
void applyDarkTitleBar (juce::Component& c)
{
    if (auto* peer = c.getPeer())
    {
        auto hwnd = (HWND) peer->getNativeHandle();
        BOOL dark = TRUE;
        DwmSetWindowAttribute (hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof (dark));
        COLORREF caption = RGB (5, 6, 10), text = RGB (237, 239, 248), border = RGB (26, 29, 43);
        DwmSetWindowAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof (caption));
        DwmSetWindowAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof (text));
        DwmSetWindowAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof (border));
    }
}
} // namespace al
