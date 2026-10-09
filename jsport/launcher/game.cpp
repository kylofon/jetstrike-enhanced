// game.cpp -- the files the game needs, the ripped music and starting the game.
#include "game.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <vector>

namespace {

// `dir`/`name` as it is on disk, trying the name as written, in upper and in lower case (case matters
// outside Windows; the port itself finds files whatever their case). Empty if not there.
wxString Find(const wxString& dir, const wxString& name, bool folder) {
    if (dir.empty()) return wxString();
    for (const wxString& n : {name, name.Upper(), name.Lower()}) {
        const wxString path = wxFileName(dir, n).GetFullPath();
        if (folder ? wxFileName::DirExists(path) : wxFileName::FileExists(path)) return path;
    }
    return wxString();
}

}  // namespace

GameFolder ReadGameFolder(const wxString& dir) {
    GameFolder f;
    if (dir.empty() || !wxFileName::DirExists(dir)) {
        f.missing = "the folder";
    } else if (Find(dir, "JS_CDROM.EXE", false).empty()) {
        f.missing = "JS_CDROM.EXE";
    } else {
        const wxString intro = Find(dir, "INTRO", true);
        if (intro.empty() || Find(intro, "INTRO.EXE", false).empty()) f.missing = "INTRO\\INTRO.EXE";
        for (const char* folder : {"DATA", "GFX", "MAP", "MISC", "PLANE"})
            if (f.missing.empty() && Find(dir, folder, true).empty()) f.missing = wxString(folder) + " folder";
    }
    f.cfgPath = Find(dir, "JS.CFG", false);
    f.haveCfg = !f.cfgPath.empty();
    if (!f.haveCfg && !dir.empty()) f.cfgPath = wxFileName(dir, "JS.CFG").GetFullPath();
    f.musicDir = Find(dir, "MUSIC", true);
    const bool haveMusicDir = !f.musicDir.empty();
    if (!haveMusicDir && !dir.empty()) f.musicDir = wxFileName(dir, "MUSIC").GetFullPath();
    for (int n = FIRST_TRACK; n <= LAST_TRACK; ++n) {
        const wxString name = wxString::Format("TRACK%02d.WAV", n);
        if (haveMusicDir && !Find(f.musicDir, name, false).empty())
            ++f.tracks;
        else if (f.firstMissingTrack.empty())
            f.firstMissingTrack = name;
    }
    return f;
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() { return wxFileName(LauncherDir(), "Game").GetFullPath(); }

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "jsenh");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--view", wxString::Format("%dx%d", o.viewW, o.viewH),
                               "--scale", wxString::Format("%d", o.scale)};
    if (o.fullscreen) args.push_back("--fullscreen");
    if (o.noIntro) args.push_back("--no-intro");

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
