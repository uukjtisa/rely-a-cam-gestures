#include "output/overlay.hpp"

#include <QPainterPath>

namespace racg::output {

namespace {

// On-picture colours (docs/DESIGN.md, "Fixed colours on the picture").
const QColor kSkeleton(0x5F, 0xD0, 0x6C);
const QColor kJoint(0xFF, 0xFF, 0xFF);
const QColor kAmber(0xE5, 0xAF, 0x24);
const QColor kRingTrack(0, 0, 0, 0x88);
const QColor kPill(0, 0, 0, 0xB8);
const QColor kGrid(0xFF, 0xFF, 0xFF, 0x8C);
const QColor kGridEdge(0, 0, 0, 0x59);

// MediaPipe's HAND_CONNECTIONS.
constexpr int kBones[][2] = {{0, 1},   {1, 2},   {2, 3},   {3, 4},   {0, 5},   {5, 6},   {6, 7},
                             {7, 8},   {5, 9},   {9, 10},  {10, 11}, {11, 12}, {9, 13},  {13, 14},
                             {14, 15}, {15, 16}, {13, 17}, {0, 17},  {17, 18}, {18, 19}, {19, 20}};

}  // namespace

void drawGrid(QPainter &p, const QRectF &r) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing, false);
  for (int i = 1; i < 3; ++i) {
    const double x = r.left() + r.width() * i / 3, y = r.top() + r.height() * i / 3;
    p.setPen(QPen(kGridEdge, 3));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.setPen(QPen(kGrid, 1));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
  }
  p.restore();
}

void drawRing(QPainter &p, QPointF c, float frac, bool amber, float scale) {
  const double r = 19 * scale, w = 5 * scale;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(kRingTrack, w));
  p.drawEllipse(c, r, r);
  QPen pen(amber ? kAmber : kSkeleton, w);
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
  p.drawArc(box, 90 * 16, -int(std::clamp(frac, 0.f, 1.f) * 360 * 16));
  p.restore();
}

void drawDetection(QPainter &p, const DetectionState &s, const Mapping &m, float scale) {
  if (!s.obs) return;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  for (const auto &h : s.obs->hands) {
    QPen bone(kSkeleton, 2.5 * scale);
    bone.setCapStyle(Qt::RoundCap);
    p.setPen(bone);
    for (const auto &b : kBones) p.drawLine(m(h.lm[b[0]].xy()), m(h.lm[b[1]].xy()));
    p.setPen(Qt::NoPen);
    p.setBrush(kJoint);
    for (int i = 1; i < 21; ++i) p.drawEllipse(m(h.lm[i].xy()), 2.2 * scale, 2.2 * scale);
    p.setBrush(kSkeleton);
    p.drawEllipse(m(h.lm[0].xy()), 3.5 * scale, 3.5 * scale);
  }
  QFont f = p.font();
  f.setPixelSize(int(12 * scale));
  f.setWeight(QFont::DemiBold);
  p.setFont(f);
  const QFontMetricsF fm(f);
  for (const auto &g : s.gestures) {
    const QPointF c = m(g.anchor) + QPointF(0, 30 * scale);
    drawRing(p, c, g.waiting ? 1.f : g.armed, g.waiting, scale);
    const QString label = g.waiting ? g.name + ": show " + g.confirmName + " with your other hand" : g.name;
    const double w = fm.horizontalAdvance(label) + 16 * scale, h = fm.height() + 4 * scale;
    const QRectF pill(c.x() - w / 2, c.y() + 26 * scale, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(kPill);
    p.drawRoundedRect(pill, h / 2, h / 2);
    p.setPen(g.waiting ? kAmber : kJoint);
    p.drawText(pill, Qt::AlignCenter, label);
  }
  if (s.stop > 0 && s.obs->hands.size() == 2) {
    const Vec2 mid = (s.obs->hands[0].lm[9].xy() + s.obs->hands[1].lm[9].xy()) / 2;
    drawRing(p, m(mid), s.stop, false, scale);
  }
  p.restore();
}

}  // namespace racg::output
