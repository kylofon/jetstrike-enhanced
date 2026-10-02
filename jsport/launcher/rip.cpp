// rip.cpp -- tools/cdrip.py in C++.
#include "rip.h"

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/tokenzr.h>

#include <cstdint>
#include <cstring>
#include <regex>
#include <string>

namespace {

const long SECTOR = 2352;
const long CHUNK = 75 * 8;  // sectors per read: 8 seconds of audio

void Put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

// The 44-byte header Python's wave module writes: PCM, 2 channels, 16 bits, 44100 Hz.
void WavHeader(uint8_t h[44], uint32_t dataBytes) {
    std::memcpy(h, "RIFF", 4);
    Put32(h + 4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    Put32(h + 16, 16);
    Put16(h + 20, 1);
    Put16(h + 22, 2);
    Put32(h + 24, 44100);
    Put32(h + 28, 44100 * 4);
    Put16(h + 32, 4);
    Put16(h + 34, 16);
    std::memcpy(h + 36, "data", 4);
    Put32(h + 40, dataBytes);
}

}  // namespace

bool ParseCue(const wxString& cuePath, wxString& image, std::vector<CueTrack>& tracks, wxString& error) {
    wxFFile f;
    {
        wxLogNull quiet;
        if (!f.Open(cuePath, "rb")) {
            error = wxString::Format("Couldn't open %s.", cuePath);
            return false;
        }
    }
    wxString text;
    if (!f.ReadAll(&text, wxConvISO8859_1)) {
        error = wxString::Format("Couldn't read %s.", cuePath);
        return false;
    }
    static const std::regex fileRe(R"re(^FILE "(.+)" BINARY)re");
    static const std::regex trackRe(R"re(^TRACK (\d+) (\S+))re");
    static const std::regex indexRe(R"re(^INDEX 0?1 (\d+):(\d+):(\d+))re");
    image.clear();
    tracks.clear();
    wxStringTokenizer lines(text, "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens()) {
        wxString line = lines.GetNextToken();
        line.Trim(false).Trim(true);
        const std::string s(line.mb_str(wxConvISO8859_1));
        std::smatch m;
        if (std::regex_search(s, m, fileRe)) {
            wxFileName name(wxString(m[1].str().c_str(), wxConvISO8859_1));
            name.MakeAbsolute(wxFileName(cuePath).GetPath());
            image = name.GetFullPath();
        }
        if (std::regex_search(s, m, trackRe)) {
            CueTrack t;
            t.number = std::stoi(m[1].str());
            t.type = wxString(m[2].str().c_str(), wxConvISO8859_1);
            tracks.push_back(t);
        }
        if (std::regex_search(s, m, indexRe) && !tracks.empty())
            tracks.back().start = (std::stol(m[1].str()) * 60 + std::stol(m[2].str())) * 75 + std::stol(m[3].str());
    }
    if (image.empty()) {
        error = "The .cue file names no BINARY image file.";
        return false;
    }
    for (const CueTrack& t : tracks)
        if (t.start < 0) {
            error = wxString::Format("Track %d of the .cue file has no INDEX 01.", t.number);
            return false;
        }
    return true;
}

bool RipCue(const wxString& cuePath, const wxString& outDir, const RipProgress& progress, wxString& error,
            std::vector<wxString>* written) {
    error.clear();
    wxString imagePath;
    std::vector<CueTrack> tracks;
    if (!ParseCue(cuePath, imagePath, tracks, error)) return false;
    wxLogNull quiet;
    wxFFile image;
    if (!image.Open(imagePath, "rb")) {
        error = wxString::Format("Couldn't open the image the .cue names:\n%s", imagePath);
        return false;
    }
    const long total = static_cast<long>(image.Length() / SECTOR);
    long audioSectors = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const long end = i + 1 < tracks.size() ? tracks[i + 1].start : total;
        if (tracks[i].type == "AUDIO" && end > tracks[i].start) audioSectors += end - tracks[i].start;
    }
    if (audioSectors == 0) {
        error = "The image has no audio tracks.";
        return false;
    }
    if (!wxFileName::DirExists(outDir) && !wxFileName::Mkdir(outDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        error = wxString::Format("Couldn't create %s.", outDir);
        return false;
    }

    std::vector<uint8_t> buffer(CHUNK * SECTOR);
    long done = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const CueTrack& t = tracks[i];
        const long end = i + 1 < tracks.size() ? tracks[i + 1].start : total;
        if (t.type != "AUDIO") continue;
        const long sectors = end > t.start ? end - t.start : 0;
        const wxString path = wxFileName(outDir, wxString::Format("TRACK%02d.WAV", t.number)).GetFullPath();
        wxFFile out;
        if (!out.Open(path, "wb")) {
            error = wxString::Format("Couldn't write %s.", path);
            return false;
        }
        uint8_t header[44];
        WavHeader(header, static_cast<uint32_t>(sectors * SECTOR));
        bool ok = out.Write(header, sizeof header) == sizeof header &&
                  image.Seek(static_cast<wxFileOffset>(t.start) * SECTOR);
        for (long at = 0; ok && at < sectors; at += CHUNK) {
            const long n = sectors - at < CHUNK ? sectors - at : CHUNK;
            const size_t bytes = static_cast<size_t>(n * SECTOR);
            ok = image.Read(buffer.data(), bytes) == bytes && out.Write(buffer.data(), bytes) == bytes;
            done += n;
            if (ok && progress && !progress(t.number, static_cast<double>(done) / audioSectors)) {
                out.Close();
                wxRemoveFile(path);
                return false;  // stopped, error empty
            }
        }
        if (!ok || !out.Close()) {
            out.Close();
            wxRemoveFile(path);
            error = wxString::Format("Couldn't rip track %d to %s.", t.number, path);
            return false;
        }
        if (written) written->push_back(path);
    }
    return true;
}
