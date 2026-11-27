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

// MediaPipe's face mesh contours, as closed or open polylines (face_mesh_connections).
constexpr int kOval[] = {10,  338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377,
                         152, 148, 176, 149, 150, 136, 172, 58,  132, 93,  234, 127, 162, 21,  54,  103, 67,  109, 10};
constexpr int kLipsOut[] = {61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 409, 270, 269, 267, 0, 37, 39, 40, 185, 61};
constexpr int kLipsIn[] = {78, 95, 88, 178, 87, 14, 317, 402, 318, 324, 308, 415, 310, 311, 312, 13, 82, 81, 80, 191, 78};
constexpr int kEyeL[] = {263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466, 263};
constexpr int kEyeR[] = {33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246, 33};
constexpr int kBrowL1[] = {276, 283, 282, 295, 285}, kBrowL2[] = {300, 293, 334, 296, 336};
constexpr int kBrowR1[] = {46, 53, 52, 65, 55}, kBrowR2[] = {70, 63, 105, 66, 107};
constexpr int kNose[] = {168, 6, 197, 195, 5, 4};

template <size_t N>
std::vector<Vec2> line(const vision::Face &f, const int (&idx)[N]) {
  std::vector<Vec2> v;
  v.reserve(N);
  for (int i : idx) v.push_back(f.lm[size_t(i)].xy());
  return v;
}

}  // namespace

std::vector<std::vector<Vec2>> faceContours(const vision::Face &f) {
  std::vector<std::vector<Vec2>> out = {line(f, kOval),   line(f, kLipsOut), line(f, kLipsIn), line(f, kEyeL),
                                        line(f, kEyeR),   line(f, kBrowL1),  line(f, kBrowL2), line(f, kBrowR1),
                                        line(f, kBrowR2), line(f, kNose)};
  // The irises (468-472 and 473-477): a short line across each, so they read as pupils.
  out.push_back({f.lm[469].xy(), f.lm[471].xy()});
  out.push_back({f.lm[474].xy(), f.lm[476].xy()});
  return out;
}

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

void drawDetection(QPainter &p, const DetectionState &s, const Mapping &m, float scale, bool hands, bool face) {
  if (!s.obs) return;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  if (face) {
    QPen pen(kSkeleton, 1.6 * scale);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    for (const auto &l : s.face) {
      QPolygonF poly;
      for (const Vec2 &v : l) poly << m(v);
      p.drawPolyline(poly);
    }
  }
  if (hands)
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
    if (g.face ? !face : !hands) continue;
    if (!g.box.isEmpty()) {
      QPen pen(kJoint, 2 * scale, Qt::DashLine);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(m(Vec2{float(g.box.left()), float(g.box.top())}),
                        m(Vec2{float(g.box.right()), float(g.box.bottom())})).normalized());
    }
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
  if (hands && s.stop > 0 && s.obs->hands.size() == 2) {
    const Vec2 mid = (s.obs->hands[0].lm[9].xy() + s.obs->hands[1].lm[9].xy()) / 2;
    drawRing(p, m(mid), s.stop, false, scale);
  }
  p.restore();
}

}  // namespace racg::output
