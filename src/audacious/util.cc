/*
 * util.c
 * Copyright 2009-2013 John Lindgren and Michał Lipski
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include "util.h"

#ifdef _WIN32
#include <windows.h>

#include <new>
#include <stdint.h>
#include <string>
#include <string.h>
#include <vector>
#include <wchar.h>

#include <libaudcore/drct.h>
#include <libaudcore/interface.h>
#include <libaudcore/mainloop.h>
#include <libaudcore/runtime.h>
#include <libaudcore/tuple.h>

#ifdef WORDS_BIGENDIAN
#define UTF16_NATIVE "UTF-16BE"
#else
#define UTF16_NATIVE "UTF-16LE"
#endif

static wchar_t ** get_argvw(int * argc)
{
    wchar_t * cmdline = GetCommandLineW();
    wchar_t ** argvw = CommandLineToArgvW(cmdline, argc);
    if (!argvw)
        throw std::bad_alloc();

    return argvw;
}

Index<String> get_argv_utf8()
{
    int argc = 0;
    auto argvw = get_argvw(&argc);

    Index<String> argv;
    argv.insert(0, argc);

    for (int i = 0; i < argc; i++)
        argv[i] = String(str_convert((char *)argvw[i],
                                     wcslen(argvw[i]) * sizeof(wchar_t),
                                     UTF16_NATIVE, "UTF-8"));

    LocalFree(argvw);
    return argv;
}

int exec_argv0()
{
    int argc = 0;
    auto argvw = get_argvw(&argc);

    std::wstring quoted = L"\"";
    quoted.append(argvw[0]);
    quoted.append(L"\"");

    _wexeclp(argvw[0], quoted.c_str(), (wchar_t*)NULL);

    /* should not get here */
    LocalFree(argvw);
    return -1;
}

namespace
{

/* Kept alive for the whole process lifetime so that the named mutex stays
 * owned; the OS releases it automatically when the process exits. */
HANDLE single_instance_mutex = nullptr;

/* Returns true if the process identified by "pid" is running the same
 * executable as we are (compared by base file name). */
static bool is_same_executable(DWORD pid, const wchar_t * self_name)
{
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return false;

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(proc, 0, path, &size) != FALSE;
    CloseHandle(proc);

    if (!ok)
        return false;

    const wchar_t * base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return _wcsicmp(base, self_name) == 0;
}

/* Brings a window to the front without altering its state.  A plain SW_SHOW
 * would un-maximize a maximized window, so pick the show command that matches
 * the current placement: a maximized window stays maximized, a minimized one
 * is restored, and a normal one is simply shown. */
static void raise_window(HWND hwnd)
{
    if (!hwnd)
        return;

    WINDOWPLACEMENT wp;
    wp.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &wp);

    if (wp.showCmd == SW_SHOWMINIMIZED)
        ShowWindow(hwnd, SW_RESTORE);
    else if (wp.showCmd == SW_SHOWMAXIMIZED)
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    else
        ShowWindow(hwnd, SW_SHOW);

    /* SetForegroundWindow() is only permitted for the foreground process, so
     * briefly attach our input queue to the current foreground thread. */
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread =
        foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    DWORD current_thread = GetCurrentThreadId();

    bool attached = foreground_thread && foreground_thread != current_thread &&
                    AttachThreadInput(current_thread, foreground_thread, TRUE);

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    if (attached)
        AttachThreadInput(current_thread, foreground_thread, FALSE);
}

struct FindWindowState
{
    const wchar_t * exe_name;
    DWORD self_pid;
    HWND visible; /* first visible, unowned top-level window */
    HWND any;     /* first unowned top-level window, visible or not */
};

static BOOL CALLBACK find_window_cb(HWND hwnd, LPARAM param)
{
    auto state = (FindWindowState *) param;

    /* Skip owned windows (dialogs, popups, ...): the main window has no
     * owner. */
    if (GetWindow(hwnd, GW_OWNER))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == state->self_pid)
        return TRUE;

    if (!is_same_executable(pid, state->exe_name))
        return TRUE;

    if (!state->any)
        state->any = hwnd;

    if (IsWindowVisible(hwnd))
    {
        state->visible = hwnd;
        return FALSE; /* stop enumerating */
    }

    return TRUE;
}

struct FindOwnState
{
    DWORD self_pid;
    HWND result;
};

static BOOL CALLBACK find_own_cb(HWND hwnd, LPARAM param)
{
    auto state = (FindOwnState *) param;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != state->self_pid || GetWindow(hwnd, GW_OWNER) ||
        !IsWindowVisible(hwnd))
        return TRUE;

    state->result = hwnd;
    return FALSE;
}

/* Raises our own main window (we are running in the instance that owns it). */
static void activate_own_window()
{
    FindOwnState state = {GetCurrentProcessId(), nullptr};
    EnumWindows(find_own_cb, (LPARAM) &state);
    raise_window(state.result);
}

/* ------------------------------------------------------------------ *
 * IPC: a hidden top-level window of a well-known class name receives a
 * WM_COPYDATA message carrying the command line of a later launch.
 * ------------------------------------------------------------------ */

static const uint32_t ipc_magic = 0x41554431; /* "AUD1" */

static wchar_t ipc_class[64];
static HWND ipc_hwnd = nullptr;
static QueuedFunc ipc_queued;

static void ipc_class_name(int instance)
{
    if (instance == 1)
        wcscpy(ipc_class, L"AudaciousIPC");
    else
        swprintf(ipc_class, 64, L"AudaciousIPC-%d", instance);
}

/* Runs in the main thread (queued from the window procedure). */
static void ipc_apply(Win32IpcMode mode, const std::vector<std::string> & files)
{
    if (!files.empty())
    {
        Index<PlaylistAddItem> items;

        for (auto & file : files)
            items.append(String(file.c_str()));

        if (mode == Win32IpcMode::Enqueue)
            aud_drct_pl_add_list(std::move(items), -1);
        else if (mode == Win32IpcMode::EnqueueToTemp)
            aud_drct_pl_open_temp_list(std::move(items));
        else if (mode == Win32IpcMode::NowPlaying)
            aud_drct_pl_add_to_now_playing(std::move(items));
        else
            aud_drct_pl_open_list(std::move(items));
    }

    /* Make sure the user actually sees the result. */
    aud_ui_show(true);
    activate_own_window();
}

/* Payload layout: u32 magic, u32 mode, u32 count,
 * then count x (u32 length, length bytes of UTF-8). */
static void ipc_handle(const void * data, size_t len)
{
    auto p = (const unsigned char *) data;
    const unsigned char * end = p + len;

    auto get_u32 = [&p, end](uint32_t & value) -> bool {
        if ((size_t)(end - p) < sizeof(uint32_t))
            return false;

        memcpy(&value, p, sizeof(uint32_t));
        p += sizeof(uint32_t);
        return true;
    };

    uint32_t magic, mode, count;
    if (!get_u32(magic) || magic != ipc_magic || !get_u32(mode) ||
        !get_u32(count))
        return;

    std::vector<std::string> files;

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t length;
        if (!get_u32(length) || (size_t)(end - p) < (size_t) length)
            return;

        files.emplace_back((const char *) p, (size_t) length);
        p += length;
    }

    if (mode > (uint32_t) Win32IpcMode::NowPlaying)
        mode = (uint32_t) Win32IpcMode::Open;

    AUDINFO("Forwarded command received: mode %d, %d file(s).\n", (int) mode,
            (int) files.size());

    ipc_queued.queue([mode, files]() {
        ipc_apply((Win32IpcMode) mode, files);
    });
}

static LRESULT CALLBACK ipc_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COPYDATA)
    {
        auto cds = (const COPYDATASTRUCT *) lp;
        if (cds && cds->lpData && cds->cbData)
            ipc_handle(cds->lpData, cds->cbData);

        return TRUE;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} /* namespace */

bool win32_claim_single_instance(int instance)
{
    wchar_t name[64];

    if (instance == 1)
        wcscpy(name, L"Local\\Audacious");
    else
        swprintf(name, 64, L"Local\\Audacious-%d", instance);

    single_instance_mutex = CreateMutexW(nullptr, FALSE, name);

    return !(single_instance_mutex &&
             GetLastError() == ERROR_ALREADY_EXISTS);
}

void win32_activate_existing_instance()
{
    wchar_t self_path[MAX_PATH];
    DWORD size = GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    if (!size || size >= MAX_PATH)
        return;

    const wchar_t * self_name = wcsrchr(self_path, L'\\');
    self_name = self_name ? self_name + 1 : self_path;

    FindWindowState state = {self_name, GetCurrentProcessId(), nullptr,
                             nullptr};
    EnumWindows(find_window_cb, (LPARAM) &state);

    raise_window(state.visible ? state.visible : state.any);
}

void win32_ipc_start(int instance)
{
    if (ipc_hwnd)
        return;

    ipc_class_name(instance);

    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ipc_wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = ipc_class;
    RegisterClassExW(&wc);

    /* Hidden (never shown) top-level window, so that FindWindowW() can locate
     * it from another process. */
    ipc_hwnd =
        CreateWindowExW(0, ipc_class, L"Audacious IPC", 0, 0, 0, 0, 0,
                        nullptr, nullptr, hinst, nullptr);
}

void win32_ipc_stop()
{
    ipc_queued.stop();

    if (ipc_hwnd)
    {
        DestroyWindow(ipc_hwnd);
        ipc_hwnd = nullptr;
    }

    if (ipc_class[0])
    {
        UnregisterClassW(ipc_class, GetModuleHandleW(nullptr));
        ipc_class[0] = 0;
    }
}

bool win32_ipc_send(int instance, Win32IpcMode mode,
                    const Index<String> & files)
{
    wchar_t name[64];

    if (instance == 1)
        wcscpy(name, L"AudaciousIPC");
    else
        swprintf(name, 64, L"AudaciousIPC-%d", instance);

    /* The primary instance may still be starting up. */
    HWND hwnd = nullptr;
    for (int i = 0; i < 50 && !hwnd; i++)
    {
        hwnd = FindWindowW(name, nullptr);
        if (!hwnd)
            Sleep(100);
    }

    if (!hwnd)
        return false;

    std::string buf;

    auto put_u32 = [&buf](uint32_t value) {
        buf.append((const char *) &value, sizeof(value));
    };

    put_u32(ipc_magic);
    put_u32((uint32_t) mode);
    put_u32((uint32_t) files.len());

    for (auto & file : files)
    {
        size_t length = strlen(file);
        put_u32((uint32_t) length);
        buf.append((const char *) file, length);
    }

    COPYDATASTRUCT cds;
    cds.dwData = ipc_magic;
    cds.cbData = (DWORD) buf.size();
    cds.lpData = (void *) buf.data();

    DWORD_PTR result = 0;
    SendMessageTimeoutW(hwnd, WM_COPYDATA, (WPARAM) nullptr, (LPARAM) &cds,
                        SMTO_ABORTIFHUNG, 5000, &result);

    return result != 0;
}

#endif
