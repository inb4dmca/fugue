// ttmetrics: the TrueType half of render/dsmaptext.h (MeasureText), on Qt's font stack.
//
// Stock DreamSeeker measures a TrueType family with GDI: CreateFont(lfHeight = -em), per-character advances, and
// tmAscent and tmDescent. The advances match the hinted advances QFontMetrics::horizontalAdvance returns for the same
// font file at pixel size em. tmAscent and tmDescent come from the font's VDMX table for that ppem, which Qt does not
// use, so they are read from the table here, falling back to Qt's ascent and descent when there is none.
// A family the host lacks returns false, and dsmaptext then asks for Arial, as stock does when a family is missing.
// Liberation Sans, Serif and Mono stand in for Arial, Times New Roman and Courier New. They have the same advances
// but no VDMX table, so on a Liberation-only host the MeasureText width matches stock and the height may be a pixel
// short.
#pragma once

namespace fugue::shell {

// Installs the resolver into render::setTrueTypeResolver. Needs a QGuiApplication; call once after constructing it.
void installTrueTypeMetrics();

// Installs render::setGlyphRasterizer, which supplies the glyph shapes for atom.maptext (render/maptextpaint.h).
// Positions and line breaks come from dsmaptext's layout, not from Qt. TrueType families are drawn antialiased.
// Stock bitmap faces ('MS Sans Serif', 'Small Fonts', 'Courier') are not shipped here, so they are drawn without
// antialiasing, in the host's face of that name if present, else its sans-serif or monospace face. That stand-in is
// not graded against stock pixels.
void installMaptextGlyphs();

}  // namespace fugue::shell
