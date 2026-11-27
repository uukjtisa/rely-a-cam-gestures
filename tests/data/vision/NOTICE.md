# Vision test data

- The `.jpg` images are Google's MediaPipe test assets, from the public
  `storage.googleapis.com/mediapipe-assets` bucket, under the Apache License 2.0.
- `reference.json` is what Google's MediaPipe 0.10.21 (Python, IMAGE mode) finds in them, made by
  `scripts/make_vision_reference.py` with the same `.task` bundles `scripts/fetch_models.py` pins.
  `racg-tests vision` checks that this project's ONNX pipeline finds the same things.
