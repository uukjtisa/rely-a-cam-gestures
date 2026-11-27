// The one place colours and sizes live (docs/DESIGN.md). They're Rely-A-Cam's dock tokens (OBS 31's
// Yami theme), so this window reads as part of Rely-A-Cam when the dock opens it. Nothing else in
// src/ui may write a colour literal; scripts/check_design.py enforces it.
#pragma once

#include <QColor>
#include <QString>

namespace racg::ui::theme {

inline const QColor grey1{0x5B, 0x62, 0x73}, grey2{0x4E, 0x55, 0x66}, grey3{0x46, 0x4B, 0x59},
    grey4{0x3C, 0x40, 0x4D}, grey5{0x32, 0x35, 0x40}, grey6{0x27, 0x2A, 0x33}, grey7{0x1D, 0x1F, 0x26},
    grey8{0x13, 0x14, 0x1A};
inline const QColor blue1{0x71, 0x8C, 0xDC}, blue2{0x47, 0x6B, 0xD7}, blue3{0x28, 0x4C, 0xB8};
inline const QColor red3{0xC0, 0x1C, 0x37}, green3{0x25, 0xA2, 0x31}, yellow2{0xE5, 0xAF, 0x24};
inline const QColor textColor{0xFF, 0xFF, 0xFF}, textLight{0xD6, 0xD6, 0xD6}, muted{0x99, 0x99, 0x99};
inline const QColor need{0xC4, 0x9A, 0x3A};        // "needs OBS" on a row
inline const QColor warnFill{0x5A, 0x20, 0x30};    // Delete, the REC chip
inline const QColor banner{0x1D, 0x2A, 0x4D}, bannerText{0xCF, 0xE0, 0xFF};
inline const QColor skeleton{0x5F, 0xD0, 0x6C};    // the detection overlay (Rely-A-Cam's note green)
inline const QColor onImage{0xFF, 0xFF, 0xFF};     // text and marks on the picture
inline const QColor scrim{0, 0, 0, 0xB0};          // pills on the picture
inline const QColor ringTrack{0, 0, 0, 0x88};
inline const QColor gridLine{0xFF, 0xFF, 0xFF, 0x8C};   // 55 %
inline const QColor gridEdge{0, 0, 0, 0x59};
inline const QColor toastFill{0xFF, 0xFF, 0xFF}, toastText{0x11, 0x11, 0x11};

constexpr int radius = 4;
constexpr int sideWidth = 360;
constexpr int ring = 46, ringStroke = 5;
constexpr int toastMs = 2200;

// The application stylesheet built from the tokens above.
QString styleSheet();
QString hex(const QColor &c);

}  // namespace racg::ui::theme
