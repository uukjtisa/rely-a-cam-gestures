"""Fetch Google's MediaPipe models and convert them to ONNX for Rely-A-Cam-Gestures.

    python scripts/fetch_models.py [--out models] [--check]

The models are Google's (Apache-2.0), downloaded from Google's own bucket and checked against
pinned SHA-256 hashes. Each .task bundle is a zip of TFLite models; each model is converted with
tf2onnx and written to <out>/<name>.onnx next to manifest.json (sizes, hashes, inputs, outputs).

--check runs every converted model and its TFLite original on the same random inputs and fails
if any output differs by more than a small tolerance. Run it after changing a pin or a converter
version; build.bat runs the plain fetch only.

Needs: tensorflow==2.15.1, tf2onnx==1.16.1, onnx==1.15.0, onnxruntime==1.17.3, numpy<2,
protobuf==3.20.3 (build.bat installs them into .deps/py).
"""
import argparse
import hashlib
import io
import json
import os
import sys
import tempfile
import urllib.request
import zipfile

BASE = "https://storage.googleapis.com/mediapipe-models"
BUNDLES = {
    "face_landmarker.task": (
        f"{BASE}/face_landmarker/face_landmarker/float16/1/face_landmarker.task",
        "64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff"),
    "hand_landmarker.task": (
        f"{BASE}/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
        "fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1"),
    "pose_landmarker_lite.task": (
        f"{BASE}/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
        "59929e1d1ee95287735ddd833b19cf4ac46d29bc7afddbbf6753c459690d574a"),
    "gesture_recognizer.task": (
        f"{BASE}/gesture_recognizer/gesture_recognizer/float16/1/gesture_recognizer.task",
        "97952348cf6a6a4915c2ea1496b4b37ebabc50cbbf80571435643c455f2b0482"),
}
# output name -> (bundle, path inside the bundle; a nested bundle is "outer.task/inner.tflite")
MODELS = {
    "face_detector": ("face_landmarker.task", "face_detector.tflite"),
    "face_landmarks": ("face_landmarker.task", "face_landmarks_detector.tflite"),
    "face_blendshapes": ("face_landmarker.task", "face_blendshapes.tflite"),
    "palm_detector": ("hand_landmarker.task", "hand_detector.tflite"),
    "hand_landmarks": ("hand_landmarker.task", "hand_landmarks_detector.tflite"),
    "pose_detector": ("pose_landmarker_lite.task", "pose_detector.tflite"),
    "pose_landmarks": ("pose_landmarker_lite.task", "pose_landmarks_detector.tflite"),
    "gesture_embedder": ("gesture_recognizer.task", "hand_gesture_recognizer.task/gesture_embedder.tflite"),
    "gesture_classifier": ("gesture_recognizer.task", "hand_gesture_recognizer.task/canned_gesture_classifier.tflite"),
}
OPSET = 17
CONVERTER = "tf2onnx 1.16.1, opset 17"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch(name, cache):
    url, want = BUNDLES[name]
    path = os.path.join(cache, name)
    if os.path.exists(path):
        data = open(path, "rb").read()
        if sha256(data) == want:
            return data
    print(f"  downloading {name}")
    with urllib.request.urlopen(url, timeout=120) as r:
        data = r.read()
    got = sha256(data)
    if got != want:
        sys.exit(f"{name}: SHA-256 {got} is not the pinned {want}. Google changed the file; "
                 "check it, then update the pin in scripts/fetch_models.py.")
    with open(path, "wb") as f:
        f.write(data)
    return data


def member(bundle_bytes, inner):
    z = zipfile.ZipFile(io.BytesIO(bundle_bytes))
    if "/" in inner:
        outer, rest = inner.split("/", 1)
        return member(z.read(outer), rest)
    return z.read(inner)


def densify(tflite_bytes):
    """Bake DENSIFY results in as plain constants. tf2onnx's TFLite reader crashes (a native
    segfault) on sparse weights, which the pose detector stores. The values come from the TFLite
    interpreter itself, so they're exactly what TFLite computes."""
    import numpy as np
    import tensorflow as tf
    from tensorflow.lite.python import schema_py_generated as S
    from tensorflow.lite.tools import flatbuffer_utils as fu
    m = fu.convert_bytearray_to_object(bytearray(tflite_bytes))
    g = m.subgraphs[0]
    codes = [max(c.builtinCode, c.deprecatedBuiltinCode) for c in m.operatorCodes]
    ops = [op for op in g.operators if codes[op.opcodeIndex] == S.BuiltinOperator.DENSIFY]
    if not ops:
        return tflite_bytes
    interp = tf.lite.Interpreter(model_content=tflite_bytes, experimental_preserve_all_tensors=True)
    interp.allocate_tensors()
    for d in interp.get_input_details():
        interp.set_tensor(d["index"], np.zeros(d["shape"], d["dtype"]))
    interp.invoke()
    for op in ops:
        buf = S.BufferT()
        buf.data = np.frombuffer(interp.get_tensor(op.outputs[0]).tobytes(), np.uint8)
        m.buffers.append(buf)
        g.tensors[op.outputs[0]].buffer = len(m.buffers) - 1
        src = g.tensors[op.inputs[0]]        # the sparse original, now unused: dense as well
        src.sparsity = None
        src.buffer = len(m.buffers) - 1
    g.operators = [op for op in g.operators if codes[op.opcodeIndex] != S.BuiltinOperator.DENSIFY]
    return bytes(fu.convert_object_to_bytearray(m))


def convert(tflite_bytes, out_path):
    import tf2onnx
    tflite_bytes = densify(tflite_bytes)
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "m.tflite")
        with open(src, "wb") as f:
            f.write(tflite_bytes)
        model_proto, _ = tf2onnx.convert.from_tflite(src, opset=OPSET, output_path=out_path)
    return model_proto


def io_of(onnx_path):
    import onnxruntime as ort
    s = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    return ([{"name": i.name, "shape": i.shape} for i in s.get_inputs()],
            [{"name": o.name, "shape": o.shape} for o in s.get_outputs()])


def sample_input(model, name, shape, rng):
    import numpy as np
    shape = [int(d) if str(d).lstrip("-").isdigit() and int(d) > 0 else 1 for d in shape]
    if model == "face_blendshapes":          # landmark pixel coordinates of a 256x256 crop
        return (rng.random(shape) * 256).astype(np.float32)
    if model == "gesture_embedder":
        if "handedness" in name:
            return rng.integers(0, 2, shape).astype(np.float32)
        if "world" in name:
            return ((rng.random(shape) - 0.5) * 0.2).astype(np.float32)
        return rng.random(shape).astype(np.float32)
    if model == "gesture_classifier":
        return (rng.random(shape) - 0.5).astype(np.float32)
    return rng.random(shape).astype(np.float32)   # images: RGB in [0, 1]


def check(model, tflite_bytes, onnx_path):
    """Same inputs into TFLite and ONNX Runtime; returns the worst relative difference."""
    import numpy as np
    import onnxruntime as ort
    import tensorflow as tf
    rng = np.random.default_rng(7)
    interp = tf.lite.Interpreter(model_content=tflite_bytes)
    interp.allocate_tensors()
    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    worst = 0.0
    for _ in range(3):
        feeds = {}
        for d, o in zip(interp.get_input_details(), sess.get_inputs()):
            x = sample_input(model, d["name"], list(d["shape"]), rng)
            interp.set_tensor(d["index"], x)
            feeds[o.name] = x
        interp.invoke()
        ref = [interp.get_tensor(d["index"]) for d in interp.get_output_details()]
        got = sess.run(None, feeds)
        # match outputs by element count + order (tf2onnx keeps TFLite's output order)
        for r, g in zip(ref, got):
            r = r.reshape(-1).astype(np.float64)
            g = np.asarray(g).reshape(-1).astype(np.float64)
            if r.size != g.size:
                raise SystemExit(f"{model}: output sizes differ ({r.size} vs {g.size})")
            scale = max(1e-3, float(np.abs(r).max()))
            worst = max(worst, float(np.abs(r - g).max()) / scale)
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "models"))
    ap.add_argument("--check", action="store_true", help="compare every ONNX model with its TFLite original")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    cache = os.path.join(out, ".cache")
    os.makedirs(cache, exist_ok=True)

    manifest_path = os.path.join(out, "manifest.json")
    old = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {}
    pins = {k: v[1] for k, v in BUNDLES.items()}
    if (not args.check and old.get("converter") == CONVERTER and old.get("bundles") == pins
            and all(os.path.exists(os.path.join(out, m + ".onnx")) for m in MODELS)):
        print("models: up to date")
        return

    bundles = {name: fetch(name, cache) for name in BUNDLES}
    manifest = {"converter": CONVERTER, "bundles": pins, "license": "Apache-2.0 (Google MediaPipe)",
                "models": {}}
    failed = []
    for model, (bundle, inner) in MODELS.items():
        tfl = member(bundles[bundle], inner)
        path = os.path.join(out, model + ".onnx")
        print(f"  converting {model}")
        convert(tfl, path)
        inputs, outputs = io_of(path)
        entry = {"from": f"{bundle}/{inner}", "tflite_sha256": sha256(tfl),
                 "onnx_sha256": sha256(open(path, "rb").read()), "inputs": inputs, "outputs": outputs}
        if args.check:
            diff = check(model, tfl, path)
            entry["max_rel_diff"] = round(diff, 8)
            ok = diff < 1e-3
            print(f"    parity {'OK ' if ok else 'BAD'} max relative difference {diff:.2e}")
            if not ok:
                failed.append(model)
        manifest["models"][model] = entry
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)
    if failed:
        sys.exit("parity failed: " + ", ".join(failed))
    print(f"models: {len(MODELS)} written to {out}")


if __name__ == "__main__":
    main()
