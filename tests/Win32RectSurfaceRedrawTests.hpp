#ifndef LOKA_WIN32_RECT_SURFACE_REDRAW_TESTS_HPP
#define LOKA_WIN32_RECT_SURFACE_REDRAW_TESTS_HPP

void testWin32RectSurfaceTicksRepaintOnlySurface();
void testWin32PaintOnlyChangeUnderScrollViewKeepsSiblingPixels();

void testWin32PaintOnlyChangeUnderScrollViewKeepsSiblingPixels();
void testWin32RefusedAnswerKeepsBroadFallback();
void testWin32RemovedSpritesRestoreGround();
void testWin32PaintAnswerContracts();
void testWin32EditTextPaintDelivery();
void testWin32PopupMenuPaintDelivery();

void testWin32TextUnitClipsLowerSiblingInk();

void testWin32AttributedTextUnitClipsLowerSiblingInk();
void testWin32AttributedTextShrinkRestoresGround();
void testWin32ImageViewTransparentLetterbox();
void testWin32ImageViewRemovalRestoresGround();

void testWin32ScrollViewAttributedTextShrinkRestoresGround();
void testWin32ScrollViewImageRemovalRestoresGround();

void testWin32WindowAndViewportGroundRoles();
void testWin32CellGroundAndTextRoles();
void testWin32RectSurfaceGroundAndSpriteRoles();
void testWin32LiveGroundLegibility();

void testWin32StaticTextInks();
void testWin32StaticTextColorChangeResidue();
void testWin32RibbonGroupEdgePixels();

void testWin32CardBandTransparentRepaintAndEmpty();
#endif
