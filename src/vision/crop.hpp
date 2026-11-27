// Cutting a rotated rectangle out of a frame into a model's input tensor, exactly the way
// MediaPipe's ImageToTensor (OpenCV path) does it, so the models see what they were trained on:
// the rectangle's corners map onto the tensor's corners, output pixel x samples at x / width
// (OpenCV's warpPerspective convention), bilinear, and pixels outside the frame are 0 (detectors)
// or the nearest edge pixel (landmark models).
#pragma once

#include "common/geometry.hpp"
#include "vision/onnx.hpp"

#include <QImage>

namespace racg::vision {

enum class Border { Zero, Replicate };

// Fills a [1, h, w, 3] RGB float tensor from `roi` of `img` (Format_RGB32 / ARGB32), pixel values
// mapped from [0, 255] to [lo, hi].
void cropToTensor(const QImage &img, const RotRect &roi, int w, int h, float lo, float hi, Border border,
                  Tensor &out);

// The point at (u, v) of `roi`, u and v in [0, 1] across and down it, in frame pixels.
inline Vec2 roiPoint(const RotRect &roi, float u, float v) {
  const Vec2 tl = roi.corner(0), tr = roi.corner(1), bl = roi.corner(3);
  return tl + (tr - tl) * u + (bl - tl) * v;
}

// MediaPipe's keep_aspect_ratio letterbox: the whole frame, padded to the tensor's aspect.
RotRect letterboxRoi(int frameW, int frameH, int tensorW, int tensorH);

}  // namespace racg::vision
