#ifndef LOKA_MAC_GROUND_HPP
#define LOKA_MAC_GROUND_HPP

#include "app/style/SurfaceGround.hpp"
#include "app/style/StyleVocab.hpp"
#import <AppKit/AppKit.h>

namespace loka
{
  namespace macos
  {
    // Deliberate platform-seam twins of ToolboxGround/ToolboxPaintGround.
    /** Answers dynamic system colors; transparent/native decline with out untouched.
        AppKit paints the window ground, but the table also exposes it for contrast. */
    bool QueryMacGroundColor(app::SurfaceGround ground, NSColor *&out);

    // Deliberate platform-seam twins: ToolboxTextInk and Win32TextRoleColor.
    /** Answers the dynamic standard text palette; UNSET leaves out untouched. */
    bool QueryMacTextColor(app::TextColor color, NSColor *&out);

    /** Standard palette text row, resolved by AppKit in the drawing appearance. */
    NSColor *MacTextRoleColor();

    /** The text color a fresh Loka label (initWithFrame:, unbezeled, no
        background) starts with. Unset text re-applies it, on plain labels and
        as the explicit foreground of unset attributed runs. */
    NSColor *MacLabelTextColor();

    /** Paints an answering ground. Declining roles assert in debug and paint
        nothing in release. */
    void MacPaintGround(app::SurfaceGround ground, NSRect rect);
  }
}

#endif
