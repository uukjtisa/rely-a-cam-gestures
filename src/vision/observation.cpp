#include "vision/observation.hpp"

#include <algorithm>
#include <cstring>

namespace racg::vision {

namespace {
// MediaPipe's face_blendshapes_graph.cc kBlendshapeNames, in model output order.
constexpr const char *kBlend[kBlendshapeCount] = {
    "_neutral", "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft",
    "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft",
    "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft",
    "jawOpen", "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
    "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft",
    "mouthPressRight", "mouthPucker", "mouthRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower",
    "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight",
    "mouthUpperUpLeft", "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight"};

// labels.txt inside Google's canned_gesture_classifier.tflite, and what the window calls them.
constexpr const char *kSignId[] = {"None", "Closed_Fist", "Open_Palm", "Pointing_Up", "Thumb_Down",
                                   "Thumb_Up", "Victory", "ILoveYou"};
constexpr const char *kSignLabel[] = {"None", "Fist", "Open palm", "Pointing up", "Thumbs down",
                                      "Thumbs up", "Victory", "I love you"};
}  // namespace

const char *signId(Sign s) { return kSignId[int(s)]; }
const char *signLabel(Sign s) { return kSignLabel[int(s)]; }

Sign Hand::sign() const {
  int best = 0;
  for (int i = 1; i < int(Sign::Count); ++i)
    if (signs[i] > signs[best]) best = i;
  return best == 0 || signs[best] < 0.5f ? Sign::None : Sign(best);
}

const char *blendshapeName(int i) { return i >= 0 && i < kBlendshapeCount ? kBlend[i] : ""; }

int blendshapeIndex(const std::string &name) {
  for (int i = 0; i < kBlendshapeCount; ++i)
    if (name == kBlend[i]) return i;
  return -1;
}

}  // namespace racg::vision
