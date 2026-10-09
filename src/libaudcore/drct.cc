/*
 * drct.c
 * Copyright 2009-2013 John Lindgren
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

#include "drct.h"

#include "hook.h"
#include "i18n.h"
#include "interface.h"
#include "internal.h"
#include "playlist-internal.h"
#include "plugins-internal.h"
#include "runtime.h"
#include "tuple.h"

#include <string.h>

/* --- PLAYBACK CONTROL --- */

EXPORT void aud_drct_play()
{
    if (aud_drct_get_playing())
    {
        if (aud_drct_get_paused())
            aud_drct_pause();
        else
        {
            int a, b;
            aud_drct_get_ab_repeat(a, b);
            aud_drct_seek(aud::max(a, 0));
        }
    }
    else
    {
        auto playlist = Playlist::active_playlist();
        playlist.set_position(playlist.get_position());
        playlist.start_playback();
    }
}

EXPORT void aud_drct_play_pause()
{
    if (aud_drct_get_playing())
        aud_drct_pause();
    else
        aud_drct_play();
}

EXPORT int aud_drct_get_position()
{
    return Playlist::playing_playlist().get_position();
}

EXPORT String aud_drct_get_filename()
{
    auto playlist = Playlist::playing_playlist();
    return playlist.entry_filename(playlist.get_position());
}

/* --- RECORDING CONTROL --- */

/* The recording plugin is currently hard-coded to FileWriter.  Someday
 * aud_drct_set_record_plugin() may be added. */

static PluginHandle * record_plugin;

static bool record_plugin_watcher(PluginHandle *, void *)
{
    if (!aud_drct_get_record_enabled())
        aud_set_bool("record", false);

    hook_call("enable record", nullptr);
    return true;
}

static void validate_record_setting(void *, void *)
{
    if (aud_get_bool("record") && !aud_drct_get_record_enabled())
    {
        /* User attempted to start recording without a recording plugin enabled.
         * This is probably not the best response, but better than nothing. */
        aud_set_bool("record", false);
        aud_ui_show_error(_("Stream recording must be configured in Audio "
                            "Settings before it can be used."));
    }
}

void record_init()
{
    auto plugin = aud_plugin_lookup_basename("filewriter");
    if (plugin && aud_plugin_get_type(plugin) == PluginType::Output)
    {
        record_plugin = plugin;
        aud_plugin_add_watch(plugin, record_plugin_watcher, nullptr);
    }

    if (!aud_drct_get_record_enabled())
        aud_set_bool("record", false);

    hook_associate("set record", validate_record_setting, nullptr);
}

void record_cleanup()
{
    hook_dissociate("set record", validate_record_setting);

    if (record_plugin)
    {
        aud_plugin_remove_watch(record_plugin, record_plugin_watcher, nullptr);
        record_plugin = nullptr;
    }
}

EXPORT PluginHandle * aud_drct_get_record_plugin()
{
    /* recording is disabled when FileWriter is the primary output plugin */
    if (!record_plugin ||
        plugin_get_enabled(record_plugin) == PluginEnabled::Primary)
        return nullptr;

    return record_plugin;
}

EXPORT bool aud_drct_get_record_enabled()
{
    return (record_plugin &&
            plugin_get_enabled(record_plugin) == PluginEnabled::Secondary);
}

EXPORT bool aud_drct_enable_record(bool enable)
{
    if (!record_plugin ||
        plugin_get_enabled(record_plugin) == PluginEnabled::Primary)
        return false;

    return plugin_enable_secondary(record_plugin, enable);
}

/* --- VOLUME CONTROL --- */

EXPORT int aud_drct_get_volume_main()
{
    StereoVolume volume = aud_drct_get_volume();
    return aud::max(volume.left, volume.right);
}

EXPORT void aud_drct_set_volume_main(int volume)
{
    StereoVolume old = aud_drct_get_volume();
    int main = aud::max(old.left, old.right);

    if (main > 0)
        aud_drct_set_volume({aud::rescale(old.left, main, volume),
                             aud::rescale(old.right, main, volume)});
    else
        aud_drct_set_volume({volume, volume});
}

EXPORT int aud_drct_get_volume_balance()
{
    StereoVolume volume = aud_drct_get_volume();

    if (volume.left == volume.right)
        return 0;
    else if (volume.left > volume.right)
        return -100 + aud::rescale(volume.right, volume.left, 100);
    else
        return 100 - aud::rescale(volume.left, volume.right, 100);
}

EXPORT void aud_drct_set_volume_balance(int balance)
{
    int main = aud_drct_get_volume_main();

    if (balance < 0)
        aud_drct_set_volume({main, aud::rescale(main, 100, 100 + balance)});
    else
        aud_drct_set_volume({aud::rescale(main, 100, 100 - balance), main});
}

/* --- PLAYLIST CONTROL --- */

EXPORT void aud_drct_pl_next()
{
    PlaylistEx playlist = Playlist::playing_playlist();
    if (playlist == Playlist())
        playlist = Playlist::active_playlist();

    playlist.next_song(aud_get_bool("repeat"));
}

EXPORT void aud_drct_pl_next_album()
{
    PlaylistEx playlist = Playlist::playing_playlist();
    if (playlist == Playlist())
        playlist = Playlist::active_playlist();

    playlist.next_album(aud_get_bool("repeat"));
}

EXPORT void aud_drct_pl_prev()
{
    PlaylistEx playlist = Playlist::playing_playlist();
    if (playlist == Playlist())
        playlist = Playlist::active_playlist();

    playlist.prev_song(aud_get_bool("repeat"));
}

EXPORT void aud_drct_pl_prev_album()
{
    PlaylistEx playlist = Playlist::playing_playlist();
    if (playlist == Playlist())
        playlist = Playlist::active_playlist();

    playlist.prev_album(aud_get_bool("repeat"));
}

static void add_list(Index<PlaylistAddItem> && items, int at, bool to_temp,
                     bool play)
{
    if (to_temp)
        Playlist::temporary_playlist().activate();

    Playlist::active_playlist().insert_items(at, std::move(items), play);
}

EXPORT void aud_drct_pl_add(const char * filename, int at)
{
    Index<PlaylistAddItem> items;
    items.append(String(filename));
    add_list(std::move(items), at, false, false);
}

EXPORT void aud_drct_pl_add_list(Index<PlaylistAddItem> && items, int at)
{
    add_list(std::move(items), at, false, false);
}

EXPORT void aud_drct_pl_open(const char * filename)
{
    Index<PlaylistAddItem> items;
    items.append(String(filename));
    add_list(std::move(items), -1, aud_get_bool("open_to_temporary"), true);
}

EXPORT void aud_drct_pl_open_list(Index<PlaylistAddItem> && items)
{
    add_list(std::move(items), -1, aud_get_bool("open_to_temporary"), true);
}

EXPORT void aud_drct_pl_open_temp(const char * filename)
{
    Index<PlaylistAddItem> items;
    items.append(String(filename));
    add_list(std::move(items), -1, true, true);
}

EXPORT void aud_drct_pl_open_temp_list(Index<PlaylistAddItem> && items)
{
    add_list(std::move(items), -1, true, true);
}

/* --- ADD TO NOW PLAYING --- */

/* A request to start playback once the background add triggered by
 * aud_drct_pl_add_to_now_playing() has finished.  Only one such request can be
 * pending at a time; a newer one replaces an older one. */
static Playlist now_playing_playlist;
static int now_playing_entry = -1;

/* Runs on the main thread after an add has been inserted. */
static void now_playing_ready(void *, void *)
{
    Playlist playlist = now_playing_playlist;
    int entry = now_playing_entry;

    now_playing_playlist = Playlist();
    now_playing_entry = -1;

    hook_dissociate("playlist add complete", now_playing_ready);

    if (!playlist.exists() || entry < 0 || entry >= playlist.n_entries())
        return;

    /* The user may have started something else while we were adding. */
    if (aud_drct_get_playing())
        return;

    playlist.set_position(entry);
    playlist.start_playback();
}

/* Returns the number of the first entry referring to "filename", or -1. */
static int find_playlist_entry(const Playlist & playlist, const char * filename)
{
    for (int entry = 0; entry < playlist.n_entries(); entry++)
    {
        String existing = playlist.entry_filename(entry);
        if (existing && !strcmp(existing, filename))
            return entry;
    }

    return -1;
}

EXPORT void aud_drct_pl_add_to_now_playing(Index<PlaylistAddItem> && items)
{
    if (!items.len())
        return;

    bool playing = aud_drct_get_playing();

    /* The "Now Playing" list is whichever list is currently playing; if nothing
     * is playing yet, it is the playlist actually named "Now Playing". */
    Playlist playlist = Playlist::playing_playlist();
    if (!playlist.exists())
        playlist = Playlist::temporary_playlist();

    String first = items[0].filename;

    /* Skip files that are already in the list. */
    Index<PlaylistAddItem> fresh;
    for (auto & item : items)
    {
        if (find_playlist_entry(playlist, item.filename) < 0)
            fresh.append(std::move(item));
    }

    /* Where the first new entry will land.  Note that insert_items() empties
     * "fresh", so remember now whether there is anything to add. */
    int first_new = playlist.n_entries();
    bool added = fresh.len() > 0;

    if (added)
    {
        playlist.activate();
        playlist.insert_items(-1, std::move(fresh), false);
    }

    if (playing)
        return; /* keep listening to the current song */

    if (added)
    {
        /* Start the new file once the background add finishes.  The "play"
         * argument of insert_items() cannot be used here, because it would wipe
         * the playlist and the queue before inserting. */
        now_playing_playlist = playlist;
        now_playing_entry = first_new;

        hook_dissociate("playlist add complete", now_playing_ready);
        hook_associate("playlist add complete", now_playing_ready, nullptr);
    }
    else
    {
        /* Nothing new to add: play the requested file where it already is. */
        int entry = find_playlist_entry(playlist, first);
        if (entry >= 0)
        {
            playlist.activate();
            playlist.set_position(entry);
            playlist.start_playback();
        }
    }
}
