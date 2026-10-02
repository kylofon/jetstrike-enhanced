// rip.h -- the CD's music tracks from a .cue/.img image to MUSIC/TRACKnn.WAV, as tools/cdrip.py does:
// the .img holds raw 2352-byte sectors, the audio sectors are 16-bit stereo PCM at 44100 Hz, and each
// audio track runs from its INDEX 01 to the next track's (or the end of the image).
#pragma once

#include <wx/string.h>

#include <functional>
#include <vector>

struct CueTrack {
    int number = 0;
    wxString type;          // AUDIO, MODE1/2352 ...
    long start = -1;        // INDEX 01 in sectors
};

// Reads the .cue: the image file it names (beside the .cue) and its tracks.
bool ParseCue(const wxString& cuePath, wxString& image, std::vector<CueTrack>& tracks, wxString& error);

// Called as the rip goes: the track being written, the part of the whole rip done (0..1).
// Returning false stops the rip (the unfinished file is removed).
using RipProgress = std::function<bool(int track, double done)>;

// Writes every audio track to <outDir>/TRACKnn.WAV (the folder is created). Returns false with `error`
// set on failure, or with `error` empty when stopped by `progress`.
bool RipCue(const wxString& cuePath, const wxString& outDir, const RipProgress& progress, wxString& error,
            std::vector<wxString>* written = nullptr);
