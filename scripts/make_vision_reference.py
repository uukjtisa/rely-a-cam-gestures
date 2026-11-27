"""MediaPipe (IMAGE mode) on the test images: the reference the C++ pipeline is diffed against."""
import glob, json, os, sys
import mediapipe as mp
from mediapipe.tasks import python as T
from mediapipe.tasks.python import vision as V

tasks = sys.argv[1]       # folder with the .task bundles
out = sys.argv[2]
imgs = sys.argv[3:]
B = lambda p: T.BaseOptions(model_asset_path=os.path.join(tasks, p))
face = V.FaceLandmarker.create_from_options(V.FaceLandmarkerOptions(base_options=B("face_landmarker.task"),
        output_face_blendshapes=True, num_faces=1))
hand = V.HandLandmarker.create_from_options(V.HandLandmarkerOptions(base_options=B("hand_landmarker.task"), num_hands=2))
pose = V.PoseLandmarker.create_from_options(V.PoseLandmarkerOptions(base_options=B("pose_landmarker_lite.task"), num_poses=1))
gest = V.GestureRecognizer.create_from_options(V.GestureRecognizerOptions(base_options=B("gesture_recognizer.task"), num_hands=2))
res = {}
for p in imgs:
    im = mp.Image.create_from_file(p)
    W, H = im.width, im.height
    px = lambda l: [round(l.x * W, 2), round(l.y * H, 2)]
    f = face.detect(im); h = hand.detect(im); o = pose.detect(im); g = gest.recognize(im)
    res[os.path.basename(p)] = {
        "size": [W, H],
        "face": [[px(l) for l in lm] for lm in f.face_landmarks],
        "blend": [{c.category_name: round(c.score, 4) for c in b} for b in f.face_blendshapes],
        "hands": [[px(l) for l in lm] for lm in h.hand_landmarks],
        "handedness": [[(c.category_name, round(c.score, 3)) for c in hd] for hd in h.handedness],
        "pose": [[px(l) + [round(l.visibility, 3)] for l in lm] for lm in o.pose_landmarks],
        "gestures": [[(c.category_name, round(c.score, 3)) for c in gg] for gg in g.gestures],
    }
    r = res[os.path.basename(p)]
    print(os.path.basename(p), W, H, "faces", len(r["face"]), "hands", len(r["hands"]), r["handedness"],
          "pose", len(r["pose"]), "gest", r["gestures"])
json.dump(res, open(out, "w"), indent=0)
