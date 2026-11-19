/*
 * media-keys.cc
 * Copyright 2026 Audacious developers
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "util.h"

#ifdef _WIN32

#include <libaudcore/drct.h>
#include <libaudcore/hook.h>
#include <libaudcore/mainloop.h>
#include <libaudcore/runtime.h>

#include <windows.h>

namespace
{

/* The media keys are system-wide; they are only grabbed while we own an
 * active playback session, so that other applications keep receiving them
 * while Audacious is idle. */
constexpr int play_pause_id = 1;
const wchar_t media_keys_class[] = L"AudaciousMediaKeys";

HWND media_keys_hwnd = nullptr;
bool play_pause_grabbed = false;
QueuedFunc media_keys_update;

void update_hotkeys()
{
    if (!media_keys_hwnd)
        return;

    /* Paused playback still counts as active, so that the key can resume it. */
    bool want_grab = aud_drct_get_playing();

    if (want_grab == play_pause_grabbed)
        return;

    if (want_grab)
    {
        play_pause_grabbed =
            RegisterHotKey(media_keys_hwnd, play_pause_id, MOD_NOREPEAT,
                           VK_MEDIA_PLAY_PAUSE) != FALSE;

        if (!play_pause_grabbed)
            AUDWARN("Could not grab the media Play/Pause key.\n");
    }
    else
    {
        UnregisterHotKey(media_keys_hwnd, play_pause_id);
        play_pause_grabbed = false;
    }
}

/* Called from libaudcore, possibly on a worker thread; the hotkey must be
 * (un)registered on the thread that owns the window. */
void playback_changed(void *, void *)
{
    media_keys_update.queue(update_hotkeys);
}

LRESULT CALLBACK media_keys_wnd_proc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp)
{
    if (msg == WM_HOTKEY && wp == play_pause_id)
    {
        aud_drct_play_pause();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void win32_media_keys_start()
{
    if (media_keys_hwnd)
        return;

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = media_keys_wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = media_keys_class;
    RegisterClassExW(&wc);

    /* Hidden window that only exists to receive WM_HOTKEY messages. */
    media_keys_hwnd = CreateWindowExW(0, media_keys_class,
                                      L"Audacious Media Keys", 0, 0, 0, 0, 0,
                                      nullptr, nullptr, hinst, nullptr);

    if (!media_keys_hwnd)
    {
        AUDWARN("Could not create the media keys window: %lu\n",
                (unsigned long) GetLastError());
        return;
    }

    hook_associate("playback ready", playback_changed, nullptr);
    hook_associate("playback stop", playback_changed, nullptr);

    update_hotkeys();
}

void win32_media_keys_stop()
{
    if (!media_keys_hwnd)
        return;

    hook_dissociate("playback ready", playback_changed, nullptr);
    hook_dissociate("playback stop", playback_changed, nullptr);
    media_keys_update.stop();

    if (play_pause_grabbed)
    {
        UnregisterHotKey(media_keys_hwnd, play_pause_id);
        play_pause_grabbed = false;
    }

    DestroyWindow(media_keys_hwnd);
    media_keys_hwnd = nullptr;

    UnregisterClassW(media_keys_class, GetModuleHandleW(nullptr));
}

#endif
