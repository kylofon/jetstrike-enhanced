// app.cpp -- the JetStrike launcher: the game folder, the CD music, the settings CONFIG.EXE kept in
// JS.CFG, the port's options, then jsport.
//
// Two developer checks run without a window (they print to the console they are started from):
//   JetStrike --selftest-cfg <JS.CFG>   reads the file, writes it back to a temporary file and checks
//                                       that both are byte-identical (and lists the settings)
//   JetStrike --rip <cue> <out folder>  rips the CD's music as the Rip button does
#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>

#include <cstdio>
#include <vector>

#ifdef __WXMSW__
#include <windows.h>
#endif

#include "jscfg.h"
#include "launcher.h"
#include "rip.h"

namespace {

// A GUI program has no console: print to the one it was started from, unless output is redirected.
void AttachConsoleOutput() {
#ifdef __WXMSW__
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!std::freopen("CONOUT$", "w", stdout)) return;
        if (!std::freopen("CONOUT$", "w", stderr)) return;
    }
#endif
}

bool ReadAll(const wxString& path, std::vector<uint8_t>& bytes) {
    wxFFile f;
    if (!f.Open(path, "rb")) return false;
    bytes.resize(static_cast<size_t>(f.Length()));
    return f.Read(bytes.data(), bytes.size()) == bytes.size();
}

int SelfTestCfg(const wxString& path) {
    std::vector<uint8_t> original;
    if (!ReadAll(path, original)) {
        std::printf("FAIL: can't read %s\n", static_cast<const char*>(path.utf8_str()));
        return 1;
    }
    JsCfg cfg;
    wxString error;
    if (!ReadCfg(path, cfg, error)) {
        std::printf("FAIL: JS.CFG %s\n", static_cast<const char*>(error.utf8_str()));
        return 1;
    }
    std::printf("music %d, effects %d, detail %d, joystick %d (calibration %d %d %d %d), SB IRQ %d\n",
                cfg[CFG_MUSIC], cfg[CFG_SFX], cfg[CFG_DETAIL], cfg[CFG_JOY], cfg[CFG_JOY_LEFT], cfg[CFG_JOY_UP],
                cfg[CFG_JOY_RIGHT], cfg[CFG_JOY_DOWN], cfg[CFG_SB_IRQ]);
    for (int i = 0; i < KEY_SLOT_COUNT; ++i)
        std::printf("  %-40s %s (0x%02x)\n", KEY_SLOTS[i].action,
                    static_cast<const char*>(KeyName(cfg[KEY_SLOTS[i].word]).utf8_str()), cfg[KEY_SLOTS[i].word]);
    const wxString temp = wxFileName::CreateTempFileName("jscfg");
    if (temp.empty() || !WriteCfg(temp, cfg, error)) {
        std::printf("FAIL: can't write a temporary file\n");
        return 1;
    }
    std::vector<uint8_t> written;
    const bool readBack = ReadAll(temp, written);
    wxRemoveFile(temp);
    if (!readBack || written != original) {
        std::printf("FAIL: written %zu bytes differ from the %zu read\n", written.size(), original.size());
        return 1;
    }
    std::printf("OK: %zu bytes written back byte-identical\n", written.size());
    std::printf("%s: equal to the launcher's defaults\n", CfgBytes(DefaultCfg()) == original ? "OK" : "NOTE: not");
    return 0;
}

int Rip(const wxString& cue, const wxString& out) {
    int last = -1;
    wxString error;
    std::vector<wxString> written;
    const bool ok = RipCue(
        cue, out,
        [&](int track, double) {
            if (track != last) std::printf("track %2d\n", last = track), std::fflush(stdout);
            return true;
        },
        error, &written);
    if (!ok) {
        std::printf("FAIL: %s\n", static_cast<const char*>(error.utf8_str()));
        return 1;
    }
    for (const wxString& w : written) std::printf("%s\n", static_cast<const char*>(w.utf8_str()));
    return 0;
}

}  // namespace

class LauncherApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("JetStrike");
        SetVendorName("Krzysztof Kania");
        // The developer checks, before wxWidgets' own command-line parsing (which knows no such options).
        if (argc >= 3 && (argv[1] == "--selftest-cfg" || argv[1] == "--rip")) AttachConsoleOutput();
        if (argc >= 3 && argv[1] == "--selftest-cfg") return Headless(SelfTestCfg(argv[2]));
        if (argc >= 4 && argv[1] == "--rip") return Headless(Rip(argv[2], argv[3]));
        if (!wxApp::OnInit()) return false;
        auto* dialog = new LauncherDialog;
        SetTopWindow(dialog);
        dialog->Show();
        return true;
    }
    int OnRun() override { return headless_ ? exitCode_ : wxApp::OnRun(); }

private:
    bool Headless(int code) {
        headless_ = true;
        exitCode_ = code;
        std::fflush(stdout);
        return true;  // OnRun returns the code without a main loop
    }
    bool headless_ = false;
    int exitCode_ = 0;
};

wxIMPLEMENT_APP(LauncherApp);
