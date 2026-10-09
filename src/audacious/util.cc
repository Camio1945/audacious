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
#include <string>
#include <string.h>
#include <wchar.h>

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
    HANDLE proc =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
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

struct FindWindowState
{
    const wchar_t * exe_name;
    DWORD self_pid;
    HWND visible;   /* first visible, unowned top-level window */
    HWND any;       /* first unowned top-level window, visible or not */
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

static void activate_existing_window()
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

    HWND hwnd = state.visible ? state.visible : state.any;
    if (!hwnd)
        return;

    /* Bring the window back without changing its state.  A plain SW_SHOW
     * would un-maximize a maximized window, so pick the show command that
     * matches the current placement: a maximized window stays maximized, a
     * minimized one is restored, and a normal one is simply shown. */
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

    bool attached = foreground_thread &&
                    foreground_thread != current_thread &&
                    AttachThreadInput(current_thread, foreground_thread, TRUE);

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);

    if (attached)
        AttachThreadInput(current_thread, foreground_thread, FALSE);
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

    if (single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        /* Another copy is already running for this instance number: bring its
         * window to the front instead of opening a second one. */
        activate_existing_window();
        return false;
    }

    return true;
}

#endif
