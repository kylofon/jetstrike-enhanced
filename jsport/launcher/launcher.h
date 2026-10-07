// launcher.h -- the launcher window: the game folder and jsenh, the CD music (and ripping it), the
// settings CONFIG.EXE kept in JS.CFG (music, effects, detail, joystick, keys), the port's own options,
// then Play.
#pragma once

#include <wx/dialog.h>

#include <vector>

#include "game.h"
#include "jscfg.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherDialog : public wxDialog {
public:
    LauncherDialog();

private:
    struct KeyRow {
        int word = 0;               // the JS.CFG word
        wxChoice* choice = nullptr;
        std::vector<int> codes;     // the scancode of every entry of `choice`
    };

    // Checks the folder and the program again, reads JS.CFG if the folder changed, and enables Play and
    // the controls to match.
    void UpdateState();
    void LoadCfg();
    // JS.CFG -> the controls.
    void ShowCfg();
    // The controls -> JS.CFG, written straight away.
    void CfgChanged();
    bool WriteCfgNow();
    void ShowKey(KeyRow& row, int code);
    void CaptureKey(KeyRow& row, const wxString& action);
    void DefaultKeys();
    void UpdateKeysNote();
    void RipMusic();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Save();
    void ViewChanged();             // custom fields on/off, window sizes in the scale list
    void CurrentView(int& w, int& h) const;

    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxStaticBitmap* musicIcon_ = nullptr;
    wxStaticText* musicNote_ = nullptr;
    wxButton* rip_ = nullptr;

    wxCheckBox* music_ = nullptr;
    wxCheckBox* sfx_ = nullptr;
    wxCheckBox* detail_ = nullptr;
    wxCheckBox* joystick_ = nullptr;
    wxStaticText* cfgNote_ = nullptr;
    std::vector<KeyRow> keys_;
    std::vector<wxWindow*> cfgControls_;  // enabled only with a complete game folder
    wxStaticBitmap* keysIcon_ = nullptr;
    wxStaticText* keysNote_ = nullptr;

    wxChoice* sbRate_ = nullptr;
    wxChoice* view_ = nullptr;
    wxSpinCtrl* viewW_ = nullptr;
    wxSpinCtrl* viewH_ = nullptr;
    wxStaticText* viewNote_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxCheckBox* noIntro_ = nullptr;
    wxButton* play_ = nullptr;

    GameFolder game_;
    JsCfg cfg_;
    wxString cfgFolder_;   // the folder cfg_ belongs to
    wxString cfgError_;    // why JS.CFG couldn't be read, if it couldn't
    bool loading_ = true;  // no edits are recorded while the window is built
};
