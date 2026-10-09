/*
 * util.h
 * Copyright 2014 John Lindgren
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

#ifndef AUDACIOUS_UTIL_H
#define AUDACIOUS_UTIL_H

#include <libaudcore/audstrings.h>

#ifdef _WIN32
Index<String> get_argv_utf8();
int exec_argv0();

/* Single-instance support.  There is no D-Bus session bus on Windows, so the
 * first instance owns a named mutex and a hidden IPC window; further launches
 * hand their files over to it instead of starting a second process. */

/* Takes ownership of the mutex for the given instance number.  Returns true if
 * this is the first (primary) instance, false if another copy is running. */
bool win32_claim_single_instance(int instance);

/* Brings the already-running instance's main window to the front. */
void win32_activate_existing_instance();

enum class Win32IpcMode
{
    Open,
    Enqueue,
    EnqueueToTemp
};

/* Primary instance only: creates the hidden window that receives forwarded
 * command lines. */
void win32_ipc_start(int instance);
void win32_ipc_stop();

/* Asks the running instance to open/enqueue "files" (URIs) and show its main
 * window.  Returns false if the running instance could not be reached. */
bool win32_ipc_send(int instance, Win32IpcMode mode,
                    const Index<String> & files);
#endif

#endif /* AUDACIOUS_UTIL_H */
