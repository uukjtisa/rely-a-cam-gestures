// The two overlays, drawn the same way on the preview and into the output: the grid (rule of
// thirds) and detection (hand skeletons, the gesture's name, and a ring that fills while you hold;
// amber while it waits for the confirm sign). Their colours sit on the picture, so they're fixed
// (docs/DESIGN.md lists them), not theme colours.
#pragma once

#include "common/geometry.hpp"
#include "vision/observation.hpp"

#include <QPainter>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace racg::output {

// What the detection overlay shows, made on the UI thread from each observation.
struct DetectionState {
  std::shared_ptr<const vision::Observation> obs;
  struct Gesture {
    QString name;
    float armed = 0;          // 0..1 ring
    bool waiting = false;     // amber: waiting for the confirm sign
    QString confirmName;      // "Thumbs up"
    Vec2 anchor;              // analysed-frame pixels
    bool face = false;        // the face map's label (the face gesture you're making), not a gesture's
  };
  std::vector<Gesture> gestures;
  float stop = 0;             // the stop sign's progress, 0..1
  std::vector<std::vector<Vec2>> face;   // the face map's contours (analysed-frame pixels), when on
};

// The face map's lines: jaw, brows, eyes, lips and the nose's bridge, plus the irises.
std::vector<std::vector<Vec2>> faceContours(const vision::Face &f);

// Analysed-frame pixels -> target pixels.
struct Mapping {
  float sx = 1, sy = 1, ox = 0, oy = 0;
  QPointF operator()(Vec2 p) const { return {p.x * sx + ox, p.y * sy + oy}; }
};

void drawGrid(QPainter &p, const QRectF &r);
// hands: skeletons, gesture labels, the stop ring. face: the face map and its label.
void drawDetection(QPainter &p, const DetectionState &s, const Mapping &m, float scale, bool hands = true,
                   bool face = false);
void drawRing(QPainter &p, QPointF centre, float frac, bool amber, float scale);

}  // namespace racg::output
