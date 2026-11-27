#include "ui/theme.hpp"

namespace racg::ui::theme {

QString hex(const QColor &c) {
  return c.alpha() == 255 ? c.name(QColor::HexRgb)
                          : QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 2);
}

QString styleSheet() {
  QString s = R"(
* { font-family: "Segoe UI"; font-size: 13px; color: @text; }
QMainWindow, #root { background: @grey7; }
QToolTip { background: @grey6; color: @textLight; border: 1px solid @grey3; padding: 3px 6px; }
#tools { background: @grey6; border-bottom: 1px solid @grey5; }
#side { background: @grey6; border-left: 1px solid @grey5; }
#sideFoot { border-top: 1px solid @grey5; }
#tabs { border-bottom: 1px solid @grey5; }
QPushButton#tab { background: transparent; border: 0; border-bottom: 2px solid transparent; border-radius: 0; padding: 8px; color: @muted; }
QPushButton#tab:hover { color: @textLight; }
QPushButton#tab:checked { color: @text; border-bottom-color: @blue2; }
#banner { color: @bannerText; background: @banner; border: 1px solid @blue3; border-radius: @rpx; padding: 5px 9px; font-size: 12px; }
#under, .muted, QLabel[muted="true"] { color: @muted; font-size: 12px; }
QLabel[small="true"] { font-size: 12px; }
QLabel[need="true"] { color: @need; }
QLabel[h3="true"] { color: @muted; font-size: 11px; font-weight: 600; letter-spacing: 1px; }
QLabel[result="true"] { background: @grey7; border-radius: @rpx; padding: 5px 8px; font-size: 12px; }
QLabel[icon="true"] { font-family: "Segoe UI Emoji"; font-size: 16px; }
QPushButton { background: @grey4; border: 1px solid @grey4; border-radius: @rpx; padding: 3px 9px; }
QPushButton:hover { background: @grey3; border-color: @grey1; }
QPushButton:checked, QPushButton[on="true"] { background: @blue3; border-color: @blue2; }
QPushButton[warn="true"] { background: @warnFill; border-color: @red3; }
QPushButton:disabled { color: rgba(255,255,255,0.45); background: rgba(60,64,77,0.45); border-color: transparent; }
QComboBox { combobox-popup: 0; }
QComboBox, QLineEdit, QSpinBox { background: @grey4; border: 1px solid @grey1; border-radius: @rpx; padding: 2px 6px; }
QLineEdit:disabled { color: @muted; }
QComboBox::drop-down { border: 0; width: 16px; }
QComboBox QAbstractItemView { background: @grey6; border: 1px solid @grey3; selection-background-color: @blue3; outline: 0; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: @grey3; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSlider::groove:horizontal { height: 4px; background: @grey4; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @blue2; border-radius: 2px; }
QSlider::handle:horizontal { background: @text; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QCheckBox, QRadioButton { spacing: 5px; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; border: 1px solid @grey1; background: @grey4; }
QCheckBox::indicator { border-radius: 2px; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: @blue2; border-color: @blue1; }
QCheckBox:disabled, QRadioButton:disabled { color: @muted; }
QMenu { background: @grey6; border: 1px solid @grey3; border-radius: @rpx; padding: 4px 0; }
QMenu::item { padding: 4px 14px 4px 24px; font-size: 12px; }
QMenu::item:selected { background: @blue3; }
QMenu::item:disabled { color: @muted; }
QMenu::indicator { width: 12px; height: 12px; left: 6px; }
QMenu::right-arrow { width: 8px; height: 8px; right: 6px; }
QMenu::separator { height: 1px; background: @grey4; margin: 4px 0; }
QLabel#menuHdr { color: @muted; font-size: 11px; font-weight: 600; padding: 5px 10px 2px 10px; }
QLabel#menuNote { color: @muted; font-size: 11px; padding: 4px 10px 3px 24px; }
QDialog { background: @grey6; }
QDialog QLabel[h4="true"] { color: @muted; font-size: 11px; font-weight: 600; margin-top: 4px; }
)";
  const std::pair<const char *, QColor> tokens[] = {
      {"@textLight", textLight}, {"@text", textColor},        {"@grey1", grey1},       {"@grey3", grey3},
      {"@grey4", grey4},         {"@grey5", grey5},      {"@grey6", grey6},       {"@grey7", grey7},
      {"@blue1", blue1},         {"@blue2", blue2},      {"@blue3", blue3},       {"@red3", red3},
      {"@muted", muted},         {"@need", need},        {"@warnFill", warnFill}, {"@bannerText", bannerText},
      {"@banner", banner}};
  for (const auto &[k, v] : tokens) s.replace(k, hex(v));
  s.replace("@rpx", QString("%1px").arg(radius));
  return s;
}

}  // namespace racg::ui::theme
