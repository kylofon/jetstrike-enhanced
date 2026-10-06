// icon.h -- the app icon, a jet over the sea.
#pragma once

#include <wx/bitmap.h>
#include <wx/iconbndl.h>

// The icon at `size` pixels square.
wxBitmap AppBitmap(int size);

// Every size a window needs, for SetIcons.
wxIconBundle AppIcons();
