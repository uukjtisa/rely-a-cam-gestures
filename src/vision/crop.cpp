#include "vision/crop.hpp"

#include <algorithm>
#include <cmath>

namespace racg::vision {

void cropToTensor(const QImage &img, const RotRect &roi, int w, int h, float lo, float hi, Border border,
                  Tensor &out) {
  out.shape = {1, h, w, 3};
  out.data.resize(size_t(w) * h * 3);
  const int iw = img.width(), ih = img.height();
  const auto *bits = img.constBits();
  const qsizetype bpl = img.bytesPerLine();
  const float k = (hi - lo) / 255.f;

  const Vec2 tl = roi.corner(0), tr = roi.corner(1), bl = roi.corner(3);
  const Vec2 du = (tr - tl) / float(w), dv = (bl - tl) / float(h);

  // One pixel's B, G, R (Format_RGB32 is 0xffRRGGBB: bytes B G R A in memory), or 0 outside.
  auto px = [&](int x, int y, float rgb[3]) {
    if (x < 0 || y < 0 || x >= iw || y >= ih) {
      if (border == Border::Zero) {
        rgb[0] = rgb[1] = rgb[2] = 0;
        return;
      }
      x = std::clamp(x, 0, iw - 1);
      y = std::clamp(y, 0, ih - 1);
    }
    const uchar *p = bits + y * bpl + x * 4;
    rgb[0] = p[2];
    rgb[1] = p[1];
    rgb[2] = p[0];
  };

  float *o = out.data.data();
  for (int y = 0; y < h; ++y) {
    Vec2 s = tl + dv * float(y);
    for (int x = 0; x < w; ++x, s += du) {
      const float fx = std::floor(s.x), fy = std::floor(s.y);
      const int x0 = int(fx), y0 = int(fy);
      const float ax = s.x - fx, ay = s.y - fy;
      float a[3], b[3], c[3], d[3];
      px(x0, y0, a);
      px(x0 + 1, y0, b);
      px(x0, y0 + 1, c);
      px(x0 + 1, y0 + 1, d);
      for (int ch = 0; ch < 3; ++ch) {
        const float top = a[ch] + (b[ch] - a[ch]) * ax, bot = c[ch] + (d[ch] - c[ch]) * ax;
        *o++ = (top + (bot - top) * ay) * k + lo;
      }
    }
  }
}

RotRect letterboxRoi(int frameW, int frameH, int tensorW, int tensorH) {
  RotRect r{frameW / 2.f, frameH / 2.f, float(frameW), float(frameH), 0};
  const float tensorAspect = float(tensorH) / tensorW, roiAspect = r.h / r.w;
  if (tensorAspect > roiAspect)
    r.h = r.w * tensorAspect;
  else
    r.w = r.h / tensorAspect;
  return r;
}

}  // namespace racg::vision
