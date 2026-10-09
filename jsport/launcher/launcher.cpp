// launcher.cpp -- the launcher window.
#include "launcher.h"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <map>

#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#include <shellapi.h>
#endif

#include "icon.h"
#include "rip.h"
#include "settings.h"
#include "version.h"

const char* const APP_TITLE = "JetStrike Enhanced";

namespace {

const char* const WEBSITE = "https://kkania.com";
const char* const SUPPORT = "https://buymeacoffee.com/krzysztofkania";
const char* const SECTION = "Game";

const int MIN_SCALE = 1, MAX_SCALE = 6, DEFAULT_SCALE = 3;
const int SB_RATES[] = {3906, 19920};   // the first is the default

// View sizes (--view WxH: the whole mission screen including the HUD). The last choice is a custom size.
struct ViewPreset { int w, h; const wchar_t* label; };
const ViewPreset VIEWS[] = {
    {320, 240, L"320 × 240: the original"},
    {480, 270, L"480 × 270 (16:9)"},
    {640, 360, L"640 × 360 (16:9, recommended)"},
    {640, 480, L"640 × 480 (4:3)"},
    {960, 540, L"960 × 540 (16:9)"},
};
const int N_VIEWS = sizeof VIEWS / sizeof VIEWS[0], CUSTOM_VIEW = N_VIEWS, DEFAULT_VIEW = 2;
const int VIEW_MIN_W = 320, VIEW_MAX_W = 960, VIEW_MIN_H = 240, VIEW_MAX_H = 540;

#ifdef __WXMSW__
HRESULT CALLBACK AboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
#else
// A label and a link on one line, for the portable About box.
void AddLink(wxWindow* parent, wxSizer* sizer, const wxString& label, const wxString& text, const wxString& url) {
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(new wxStaticText(parent, wxID_ANY, label + " "), 0, wxALIGN_CENTER_VERTICAL);
    line->Add(new wxHyperlinkCtrl(parent, wxID_ANY, text, url), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(line);
}
#endif

wxStaticText* GreyText(wxWindow* parent, const wxString& text = wxEmptyString) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return label;
}

// A label, a text field and a Browse button in a row of a two-column grid.
wxTextCtrl* PathRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip,
                    wxButton** browse) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(300), -1));
    text->SetToolTip(tip);
    *browse = new wxButton(parent, wxID_ANY, "B&rowse...");
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    row->Add(*browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(row, 1, wxEXPAND);
    return text;
}

// The small window that waits for a key: any key the game knows counts, Esc, Tab and Enter too, so it
// is left with the mouse (Cancel).
class KeyCaptureDialog : public wxDialog {
public:
    KeyCaptureDialog(wxWindow* parent, const wxString& action, const wxString& current)
        : wxDialog(parent, wxID_ANY, "Choose a key") {
        const int margin = FromDIP(12);
        auto* all = new wxBoxSizer(wxVERTICAL);
        auto* heading = new wxStaticText(this, wxID_ANY, wxString::Format(L"Press the key for “%s”.", action));
        heading->SetFont(GetFont().Bold());
        all->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, margin);
        all->Add(GreyText(this, wxString::Format("Now: %s. Esc, Tab and Enter count as keys too:\n"
                                                 "click Cancel to keep the key.", current)),
                 0, wxLEFT | wxRIGHT | wxTOP, margin);
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();
        buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"));
        all->Add(buttons, 0, wxEXPAND | wxALL, margin);
        SetSizerAndFit(all);
        CentreOnParent();
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
            const int code = ScancodeOf(event);
            if (code != 0) {
                code_ = code;
                EndModal(wxID_OK);
            }
            // Keys the game doesn't know are swallowed.
        });
    }
    int Code() const { return code_; }

private:
    int code_ = 0;
};

}  // namespace

LauncherDialog::LauncherDialog()
    : wxDialog(nullptr, wxID_ANY, APP_TITLE, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxMINIMIZE_BOX) {
    SetIcons(AppIcons());
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);
    cfg_ = DefaultCfg();

    // Game files
    auto* filesBox = new wxStaticBoxSizer(wxVERTICAL, this, "Game files");
    wxWindow* fb = filesBox->GetStaticBox();
    auto* filesGrid = new wxFlexGridSizer(2, gap, gap);
    filesGrid->AddGrowableCol(1);
    wxButton* browseFolder = nullptr;
    wxButton* browseProgram = nullptr;
    folder_ = PathRow(fb, filesGrid, "&Folder:",
                      "The folder with the original game's files (JS_CDROM.EXE, INTRO, DATA, GFX, MAP, MISC, PLANE ...).",
                      &browseFolder);
    program_ = PathRow(fb, filesGrid, "&Program:", "jsenh, the game.", &browseProgram);
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    statusNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    statusRow->Add(statusIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    statusRow->Add(statusNote_, 1, wxALIGN_CENTER_VERTICAL);
    auto* musicRow = new wxBoxSizer(wxHORIZONTAL);
    musicIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    musicNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    rip_ = new wxButton(fb, wxID_ANY, "Rip CD &music...");
    rip_->SetToolTip("Writes the CD's music tracks to MUSIC\\TRACK02..15.WAV in the game folder, from a CD image: "
                     "a .cue file and the .img/.bin with raw 2352-byte sectors it names (as tools/cdrip.py does).");
    musicRow->Add(musicIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    musicRow->Add(musicNote_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    musicRow->Add(rip_, 0, wxALIGN_CENTER_VERTICAL);
    filesBox->Add(filesGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(statusRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(musicRow, 0, wxEXPAND | wxALL, gap);
    folder_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    program_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    browseFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFolder(); });
    browseProgram->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseProgram(); });
    rip_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RipMusic(); });

    // The game's settings (JS.CFG)
    auto* cfgBox = new wxStaticBoxSizer(wxVERTICAL, this, "Game settings (JS.CFG)");
    wxWindow* cb = cfgBox->GetStaticBox();
    music_ = new wxCheckBox(cb, wxID_ANY, "CD &music");
    music_->SetToolTip("The CD's music tracks (MUSIC\\TRACKnn.WAV in the port).");
    sfx_ = new wxCheckBox(cb, wxID_ANY, "Sound &effects");
    sfx_->SetToolTip("The Sound Blaster's effects.");
    detail_ = new wxCheckBox(cb, wxID_ANY, "&Detail (parallax scrolling)");
    detail_->SetToolTip("CONFIG.EXE recommended it only with a VESA local bus graphics card; no cost in the port.");
    joystick_ = new wxCheckBox(cb, wxID_ANY, "&Joystick on");
    joystick_->SetToolTip("The port reads the first SDL gamepad: stick or D-pad steers, the south button (A / Cross) "
                          "fires the guns. It works with this off as well. No calibration is needed: the launcher "
                          "keeps the calibration words of JS.CFG at usable neutral values.");
    auto* checks = new wxFlexGridSizer(2, small, FromDIP(24));
    checks->Add(music_);
    checks->Add(detail_);
    checks->Add(sfx_);
    checks->Add(joystick_);
    cfgBox->Add(checks, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    cfgNote_ = GreyText(cb);
    cfgBox->Add(cfgNote_, 0, wxALL, gap);
    for (wxCheckBox* c : {music_, sfx_, detail_, joystick_}) {
        c->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { CfgChanged(); });
        cfgControls_.push_back(c);
    }

    // Port options
    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Port options");
    wxWindow* ob = optionsBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Sound &Blaster rate:"), 0, wxALIGN_CENTER_VERTICAL);
    sbRate_ = new wxChoice(ob, wxID_ANY);
    sbRate_->Append(L"3 906 Hz: what the original plays (default)");
    sbRate_->Append(L"19 920 Hz: as designed (the intro sounds wrong)");
    sbRate_->SetToolTip("The original means to play its effects at 19 920 Hz, but a bug programs the card for 3 906 Hz: "
                        "lower, slower, muffled effects, as players heard them in 1994 (--sb-rate).");
    grid->Add(sbRate_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&View size:"), 0, wxALIGN_CENTER_VERTICAL);
    view_ = new wxChoice(ob, wxID_ANY);
    for (const ViewPreset& v : VIEWS) view_->Append(v.label);
    view_->Append(L"Custom…");
    view_->SetToolTip("How much of the map you see around the plane (--view). The game draws its original pixels "
                      "at 1:1; a larger view shows more. In the front end the original 320 × 240 screens are used.");
    grid->Add(view_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Custom size:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* custom = new wxBoxSizer(wxHORIZONTAL);
    viewW_ = new wxSpinCtrl(ob, wxID_ANY, "640", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, VIEW_MIN_W,
                            VIEW_MAX_W, 640);
    viewH_ = new wxSpinCtrl(ob, wxID_ANY, "360", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, VIEW_MIN_H,
                            VIEW_MAX_H, 360);
    viewW_->SetToolTip("Width: a multiple of 16, 320 to 960.");
    viewH_->SetToolTip("Height: 240 to 540, the HUD included.");
    custom->Add(viewW_, 0, wxALIGN_CENTER_VERTICAL);
    custom->Add(new wxStaticText(ob, wxID_ANY, L" × "), 0, wxALIGN_CENTER_VERTICAL);
    custom->Add(viewH_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(custom, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Window size:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxChoice(ob, wxID_ANY);
    scale_->SetToolTip("The window's size when the game starts: the view times a whole number. A window that "
                       "doesn't fit the desktop is made smaller. Alt+Enter switches to full screen.");
    grid->Add(scale_, 0, wxALIGN_CENTER_VERTICAL);
    optionsBox->Add(grid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    fullscreen_ = new wxCheckBox(ob, wxID_ANY, "Start in f&ull screen (Alt+Enter switches)");
    noIntro_ = new wxCheckBox(ob, wxID_ANY, "&Skip the intro");
    noIntro_->SetToolTip("The intro (INTRO.EXE, which JS.BAT ran before the game) is skipped (--no-intro). "
                         "In the intro, Esc ends it.");
    optionsBox->Add(fullscreen_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    optionsBox->Add(noIntro_, 0, wxALL, gap);

    // Other keys
    auto* helpBox = new wxStaticBoxSizer(wxVERTICAL, this, "Other keys in the game");
    wxWindow* hb = helpBox->GetStaticBox();
    auto* help = new wxFlexGridSizer(2, small, FromDIP(16));
    const char* const HELP[][2] = {
        {"Space  Enter", "confirm in menus and on every screen"},
        {"1 ... 9  0", "throttle setting (0 = full)"},
        {"F1 ... F10", "save / load slot (at the briefing)"},
        {"F12", "weapon select"},
        {"Esc", "ends the intro"},
        {"Alt+Enter", "full screen (the port)"},
        {"Gamepad", "stick or D-pad steers, A / Cross fires"},
    };
    for (const auto& k : HELP) {
        help->Add(new wxStaticText(hb, wxID_ANY, k[0]));
        help->Add(GreyText(hb, k[1]));
    }
    helpBox->Add(help, 0, wxALL, gap);

    // Keys (JS.CFG)
    auto* keysBox = new wxStaticBoxSizer(wxVERTICAL, this, "Keys (JS.CFG)");
    wxWindow* kb = keysBox->GetStaticBox();
    auto* keyGrid = new wxFlexGridSizer(3, small, gap);
    keys_.reserve(KEY_SLOT_COUNT);
    for (int i = 0; i < KEY_SLOT_COUNT; ++i) {
        const KeySlot& slot = KEY_SLOTS[i];
        keys_.emplace_back();
        KeyRow& row = keys_.back();
        row.word = slot.word;
        auto* label = new wxStaticText(kb, wxID_ANY, slot.action);
        label->SetToolTip(slot.tip);
        row.choice = new wxChoice(kb, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(150), -1));
        for (int code : AssignableKeys()) {
            row.choice->Append(KeyName(code));
            row.codes.push_back(code);
        }
        row.choice->SetToolTip(slot.tip);
        auto* set = new wxButton(kb, wxID_ANY, "Set...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        set->SetToolTip("Press the key to use.");
        keyGrid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
        keyGrid->Add(row.choice, 0, wxALIGN_CENTER_VERTICAL);
        keyGrid->Add(set, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND);
        row.choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { CfgChanged(); });
        set->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { CaptureKey(keys_[i], KEY_SLOTS[i].action); });
        cfgControls_.push_back(row.choice);
        cfgControls_.push_back(set);
    }
    keysBox->Add(keyGrid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    auto* keysRow = new wxBoxSizer(wxHORIZONTAL);
    keysIcon_ = new wxStaticBitmap(kb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    keysNote_ = GreyText(kb);
    auto* defaults = new wxButton(kb, wxID_ANY, "&Original keys");
    defaults->SetToolTip("The keys of the shipped JS.CFG.");
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DefaultKeys(); });
    cfgControls_.push_back(defaults);
    keysRow->Add(keysIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    keysRow->Add(keysNote_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    keysRow->Add(defaults, 0, wxALIGN_CENTER_VERTICAL);
    keysBox->Add(keysRow, 0, wxEXPAND | wxALL, gap);

    // Buttons
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* about = new wxButton(this, wxID_ABOUT, "&About");
    play_ = new wxButton(this, wxID_ANY, "&Play");
    auto* close = new wxButton(this, wxID_CLOSE, "Close");
    buttons->Add(about);
    buttons->AddStretchSpacer();
    buttons->Add(play_, 0, wxRIGHT, gap);
    buttons->Add(close);
    play_->SetDefault();
    SetEscapeId(wxID_CLOSE);
    about->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { About(); });
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Play(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

    // Two columns: files, settings, options and the other keys on the left, the key bindings on the right.
    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(filesBox, 0, wxEXPAND);
    left->Add(cfgBox, 0, wxEXPAND | wxTOP, margin);
    left->Add(optionsBox, 0, wxEXPAND | wxTOP, margin);
    left->Add(helpBox, 1, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(left, 0, wxEXPAND);
    columns->Add(keysBox, 0, wxEXPAND | wxLEFT, margin);
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(columns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    SetSizer(all);

    // Settings from the last run.
    wxString dir = settings::GetString(SECTION, "GameFolder", "");
    wxString program = settings::GetString(SECTION, "Program", "");
    folder_->ChangeValue(dir.empty() ? DefaultGameDir() : dir);
    program_->ChangeValue(program.empty() ? DefaultProgram() : program);
    sbRate_->SetSelection(settings::GetInt(SECTION, "SoundRate", SB_RATES[0]) == SB_RATES[1] ? 1 : 0);
    view_->SetSelection(wxMax(0, wxMin(CUSTOM_VIEW, settings::GetInt(SECTION, "View", DEFAULT_VIEW))));
    viewW_->SetValue(settings::GetInt(SECTION, "ViewW", 640));
    viewH_->SetValue(settings::GetInt(SECTION, "ViewH", 360));
    ViewChanged();
    scale_->SetSelection(
        wxMax(MIN_SCALE, wxMin(MAX_SCALE, settings::GetInt(SECTION, "Scale", DEFAULT_SCALE))) - MIN_SCALE);
    view_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ViewChanged(); });
    viewW_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ViewChanged(); });
    viewH_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ViewChanged(); });
    viewW_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ViewChanged(); });
    viewH_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ViewChanged(); });
    fullscreen_->SetValue(settings::GetInt(SECTION, "Fullscreen", 0) != 0);
    noIntro_->SetValue(settings::GetInt(SECTION, "NoIntro", 0) != 0);
    loading_ = false;
    UpdateState();

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive()) UpdateState();  // files may have been copied in meanwhile
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        Save();
        Destroy();
    });

    Fit();
    if (!settings::RestoreWindowPosition(SECTION, this)) Centre();
}

void LauncherDialog::LoadCfg() {
    cfgError_.clear();
    cfg_ = DefaultCfg();
    if (game_.haveCfg) {
        JsCfg read;
        if (ReadCfg(game_.cfgPath, read, cfgError_)) cfg_ = read;
    }
    ShowCfg();
}

void LauncherDialog::ShowKey(KeyRow& row, int code) {
    for (size_t i = 0; i < row.codes.size(); ++i)
        if (row.codes[i] == code) {
            row.choice->SetSelection(static_cast<int>(i));
            return;
        }
    // A value CONFIG.EXE wouldn't write: shown as it is, and kept unless changed.
    row.choice->Append(KeyName(code));
    row.codes.push_back(code);
    row.choice->SetSelection(static_cast<int>(row.codes.size() - 1));
}

void LauncherDialog::ShowCfg() {
    const bool was = loading_;
    loading_ = true;
    music_->SetValue(cfg_[CFG_MUSIC] != 0);
    sfx_->SetValue(cfg_[CFG_SFX] != 0);
    detail_->SetValue(cfg_[CFG_DETAIL] != 0);
    joystick_->SetValue(cfg_[CFG_JOY] != 0);
    for (KeyRow& row : keys_) ShowKey(row, cfg_[row.word]);
    loading_ = was;
    UpdateKeysNote();
}

void LauncherDialog::CfgChanged() {
    if (loading_) return;
    cfg_[CFG_MUSIC] = music_->GetValue() ? 1 : 0;
    cfg_[CFG_SFX] = sfx_->GetValue() ? 1 : 0;
    cfg_[CFG_DETAIL] = detail_->GetValue() ? 1 : 0;
    cfg_[CFG_JOY] = joystick_->GetValue() ? 1 : 0;
    if (cfg_[CFG_JOY]) FixCalibration(cfg_);
    for (KeyRow& row : keys_) {
        const int sel = row.choice->GetSelection();
        if (sel >= 0) cfg_[row.word] = static_cast<uint16_t>(row.codes[sel]);
    }
    UpdateKeysNote();
    if (WriteCfgNow()) UpdateState();
}

bool LauncherDialog::WriteCfgNow() {
    if (!game_.missing.empty() || game_.cfgPath.empty()) return false;
    wxString error;
    if (!WriteCfg(game_.cfgPath, cfg_, error)) {
        wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
        return false;
    }
    cfgError_.clear();
    return true;
}

void LauncherDialog::CaptureKey(KeyRow& row, const wxString& action) {
    KeyCaptureDialog dialog(this, action, KeyName(cfg_[row.word]));
    if (dialog.ShowModal() != wxID_OK) return;
    ShowKey(row, dialog.Code());
    CfgChanged();
}

void LauncherDialog::DefaultKeys() {
    const JsCfg defaults = DefaultCfg();
    for (KeyRow& row : keys_) cfg_[row.word] = defaults[row.word];
    ShowCfg();
    CfgChanged();
}

void LauncherDialog::UpdateKeysNote() {
    std::map<int, std::vector<wxString>> users;
    for (int i = 0; i < KEY_SLOT_COUNT; ++i) users[cfg_[keys_[i].word]].push_back(KEY_SLOTS[i].action);
    wxString note;
    for (const auto& entry : users)
        if (entry.second.size() > 1) {
            note = wxString::Format("%s does two things: %s and %s.", KeyName(entry.first), entry.second[0],
                                    entry.second[1]);
            break;
        }
    keysIcon_->Show(!note.empty());
    keysNote_->SetLabel(note.empty() ? wxString("Hover over an action for what it does.") : note);
    keysNote_->Wrap(FromDIP(260));
}

void LauncherDialog::UpdateState() {
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    game_ = ReadGameFolder(dir);
    const bool haveProgram = wxFileName::FileExists(program);
    const bool haveGame = game_.missing.empty();

    // JS.CFG: read again whenever the folder changes.
    const wxString canonical = wxFileName(dir).GetFullPath();
    if (canonical != cfgFolder_) {
        cfgFolder_ = canonical;
        LoadCfg();
    }
    for (wxWindow* w : cfgControls_) w->Enable(haveGame);
    if (!haveGame)
        cfgNote_->SetLabel("Choose the game folder first.");
    else if (!cfgError_.empty())
        cfgNote_->SetLabel(wxString::Format("JS.CFG %s: shown with the original settings.", cfgError_));
    else if (game_.haveCfg)
        cfgNote_->SetLabel("Kept in JS.CFG in the game folder, the file CONFIG.EXE wrote.\nChanges are saved at once.");
    else
        cfgNote_->SetLabel("No JS.CFG yet: the original settings. It is written\nwhen you change something or play.");

    wxString note;
    if (!haveGame)
        note = game_.missing == "the folder" ? wxString("This folder isn't there.")
                                             : wxString::Format("This folder needs the game's files: %s is missing.",
                                                                game_.missing);
    else if (!haveProgram)
        note = wxString::Format("%s isn't there.", wxFileName(program).GetFullName());
    else
        note = "Found JetStrike (CD version).";
    const bool ok = haveProgram && haveGame;
    statusIcon_->Show(!ok);
    statusNote_->SetLabel(note);

    const int total = LAST_TRACK - FIRST_TRACK + 1;
    musicIcon_->Show(haveGame && game_.tracks < total);
    rip_->Enable(haveGame);
    if (!haveGame)
        musicNote_->SetLabel(wxEmptyString);
    else if (game_.tracks == total)
        musicNote_->SetLabel("CD music: all 14 tracks are in MUSIC.");
    else
        musicNote_->SetLabel(wxString::Format(
            "CD music: %s%s isn't in MUSIC, so the game plays\nwithout music. Rip the tracks from a CD image (.cue).",
            game_.tracks == 0 ? wxString("TRACK02..15.WAV") : game_.firstMissingTrack,
            game_.tracks == 0 ? wxString(" (14 tracks)") : wxString::Format(" (%d of 14 there)", game_.tracks)));
    play_->Enable(ok);
    Layout();
    Fit();
}

void LauncherDialog::RipMusic() {
    wxString cueDir = settings::GetString(SECTION, "CueFolder", "");
    wxFileDialog choose(this, "Choose the CD image's .cue file", cueDir, wxEmptyString,
                        "CD images (*.cue)|*.cue;*.CUE|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (choose.ShowModal() != wxID_OK) return;
    const wxString cue = choose.GetPath();
    settings::SetString(SECTION, "CueFolder", wxFileName(cue).GetPath());

    wxString image, error;
    std::vector<CueTrack> tracks;
    if (!ParseCue(cue, image, tracks, error)) {
        wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
        return;
    }
    int audio = 0;
    for (const CueTrack& t : tracks) audio += t.type == "AUDIO";
    if (audio == 0) {
        wxMessageBox("This CD image has no music tracks.", APP_TITLE, wxOK | wxICON_ERROR, this);
        return;
    }
    if (game_.tracks > 0 &&
        wxMessageBox(wxString::Format("Replace the music tracks in %s?", game_.musicDir), APP_TITLE,
                     wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;

    wxProgressDialog progress("Ripping the CD music", wxString::Format("Writing %d tracks to %s", audio, game_.musicDir),
                              1000, this,
                              wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME);
    std::vector<wxString> written;
    const bool ok = RipCue(
        cue, game_.musicDir,
        [&](int track, double done) {
            return progress.Update(static_cast<int>(done * 999),
                                   wxString::Format("Track %d of %d: TRACK%02d.WAV", track - 1, audio, track));
        },
        error, &written);
    progress.Hide();
    if (ok)
        wxMessageBox(wxString::Format("%zu music tracks written to %s.", written.size(), game_.musicDir), APP_TITLE,
                     wxOK | wxICON_INFORMATION, this);
    else if (!error.empty())
        wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
    UpdateState();
}

void LauncherDialog::BrowseFolder() {
    wxDirDialog dialog(this, "Choose the folder with the game's files", folder_->GetValue(),
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) folder_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherDialog::BrowseProgram() {
    wxFileName current(program_->GetValue());
#ifdef __WXMSW__
    const char* const filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const char* const filter = "All files|*";
#endif
    wxFileDialog dialog(this, "Choose jsenh", current.GetPath(), current.GetFullName(), filter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) program_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherDialog::Play() {
    Save();
    UpdateState();
    // The game stops at once without JS.CFG ("Please run config program first").
    if (!game_.haveCfg || !cfgError_.empty()) {
        if (!WriteCfgNow()) return;
        UpdateState();
    }
    GameOptions options;
    options.program = wxFileName(program_->GetValue()).GetFullPath();
    options.gameDir = wxFileName(folder_->GetValue()).GetFullPath();
    CurrentView(options.viewW, options.viewH);
    options.scale = scale_->GetSelection() + MIN_SCALE;
    options.fullscreen = fullscreen_->GetValue();
    options.sbRate = SB_RATES[sbRate_->GetSelection() == 1 ? 1 : 0];
    options.noIntro = noIntro_->GetValue();
    wxString error;
    if (!LaunchGame(options, error)) wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
}

void LauncherDialog::Save() {
    // A folder or program left at its default is stored empty, so it follows the launcher if it moves.
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    settings::SetString(SECTION, "GameFolder",
                        wxFileName(dir).SameAs(wxFileName(DefaultGameDir())) ? wxString() : dir);
    settings::SetString(SECTION, "Program",
                        wxFileName(program).SameAs(wxFileName(DefaultProgram())) ? wxString() : program);
    settings::SetInt(SECTION, "SoundRate", SB_RATES[sbRate_->GetSelection() == 1 ? 1 : 0]);
    settings::SetInt(SECTION, "View", view_->GetSelection());
    settings::SetInt(SECTION, "ViewW", viewW_->GetValue());
    settings::SetInt(SECTION, "ViewH", viewH_->GetValue());
    settings::SetInt(SECTION, "Scale", scale_->GetSelection() + MIN_SCALE);
    settings::SetInt(SECTION, "Fullscreen", fullscreen_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "NoIntro", noIntro_->GetValue() ? 1 : 0);
    settings::SaveWindowPosition(SECTION, this);
}

// The view the choice stands for; a custom width is rounded to a multiple of 16 and both are kept in range.
void LauncherDialog::CurrentView(int& w, int& h) const {
    const int sel = view_->GetSelection();
    if (sel >= 0 && sel < N_VIEWS) {
        w = VIEWS[sel].w;
        h = VIEWS[sel].h;
        return;
    }
    w = wxMax(VIEW_MIN_W, wxMin(VIEW_MAX_W, (viewW_->GetValue() + 8) / 16 * 16));
    h = wxMax(VIEW_MIN_H, wxMin(VIEW_MAX_H, viewH_->GetValue()));
}

void LauncherDialog::ViewChanged() {
    const bool custom = view_->GetSelection() == CUSTOM_VIEW;
    viewW_->Enable(custom);
    viewH_->Enable(custom);
    int w, h;
    CurrentView(w, h);
    int keep = scale_->GetSelection();
    if (keep == wxNOT_FOUND) keep = DEFAULT_SCALE - MIN_SCALE;
    scale_->Clear();
    for (int s = MIN_SCALE; s <= MAX_SCALE; ++s)
        scale_->Append(wxString::Format(L"%d × %d  (%d×)", w * s, h * s, s));
    scale_->SetSelection(keep);
}

void LauncherDialog::About() {
    const wxString title = wxString("About ") + APP_TITLE;
    const wxString heading = wxString(APP_TITLE) + " " + APP_VERSION_TEXT;
    const wxString blurb = "Starts jsenh, JetStrike Enhanced: the SDL3 port of JetStrike (PC CD version, 1994, "
                           "converted by Team Hoi Games) with a larger view, and replaces its CONFIG.EXE.";
#ifdef __WXMSW__
    // The Windows task dialog.
    const wxString content = wxString::Format(
        "%s\n\n"
        "Author: Krzysztof Kania\n"
        "Website: <a href=\"%s\">kkania.com</a>\n"
        "Support: <a href=\"%s\">buymeacoffee.com/krzysztofkania</a>",
        blurb, WEBSITE, SUPPORT);
    wxIcon icon;
    icon.CopyFromBitmap(AppBitmap(FromDIP(32)));
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof dialog;
    dialog.hwndParent = static_cast<HWND>(GetHWND());
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = title.wc_str();
    dialog.hMainIcon = static_cast<HICON>(icon.GetHICON());
    dialog.pszMainInstruction = heading.wc_str();
    dialog.pszContent = content.wc_str();
    dialog.pfCallback = AboutCallback;
    TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
#else
    wxDialog dialog(this, wxID_ANY, title);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(&dialog, wxID_ANY, AppBitmap(dialog.FromDIP(48))), 0, wxALL, dialog.FromDIP(12));

    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* headingText = new wxStaticText(&dialog, wxID_ANY, heading);
    headingText->SetFont(dialog.GetFont().Bold().Scaled(1.3f));
    text->Add(headingText, 0, wxBOTTOM, dialog.FromDIP(8));
    text->Add(new wxStaticText(&dialog, wxID_ANY, blurb), 0, wxBOTTOM, dialog.FromDIP(12));
    text->Add(new wxStaticText(&dialog, wxID_ANY, "Author: Krzysztof Kania"));
    AddLink(&dialog, text, "Website:", "kkania.com", WEBSITE);
    AddLink(&dialog, text, "Support:", "buymeacoffee.com/krzysztofkania", SUPPORT);
    body->Add(text, 1, wxTOP | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(body, 1, wxEXPAND);
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
    dialog.SetSizerAndFit(all);
    dialog.CentreOnParent();
    dialog.ShowModal();
#endif
}
