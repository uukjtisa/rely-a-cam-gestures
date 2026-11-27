# Rely-A-Cam-Gestures

Hand and face gestures for your camera on Windows. It runs on its own with a webcam, or gets
started by the [Rely-A-Cam](https://github.com/uukjtisa/rely-a-cam) OBS plugin to add gestures
to your phone cameras.

- **Gestures:** itsgiving's 14 face and body reactions, Google's 7 hand signs, and your own
  (record 3 takes, test them live, check them for misfires). A confirm sign with your other hand
  and a stop sign (both palms, acts in 0.3 s) keep them from firing by accident. Two hands making
  one pose never fire a one-hand gesture: both palms aren't "Open palm", both fists aren't "Fist".
- **Controls:** a fixed list, one gesture per job. Pinch zoom (pinch and pull back to zoom out, or
  push toward the camera to zoom in; let go and pinch again to keep going), a pinch box (aim with both fists, then pinch both; move the pinches to adjust it), zoom on what
  you hold (teach it with examples of holding things),
  follow your face (hold its gesture and pinch with the other hand to set how close), camera off,
  snapshot, OBS scenes, recording, streaming and hotkeys, and the phone's torch, mic and camera
  when attached to Rely-A-Cam. Controls that read the same move switch each other off.
- **Memes:** pictures, GIFs and videos (WebM keeps its transparency) on your head, full frame or
  in their spot, with their sound to your speakers or into calls (through VB-Cable). itsgiving's
  14 pictures are downloaded from its repository on first start and put on its reactions.
- **See what it sees:** the detection overlay (hands, holds) and a face map with the face gesture
  you're making, in the preview or the output. Reset zoom under the preview puts the whole
  picture back.
- **Outputs on its own:** its own Windows 11 virtual camera, "Rely-A-Cam Gestures", and OBS
  Virtual Camera. Inside Rely-A-Cam it draws on the phone's source in OBS instead.

## Install

Run `RelyACamGestures-Setup-<version>.exe`, or tick "Also install Rely-A-Cam-Gestures" in the
Rely-A-Cam installer. Windows 10 (1809) or 11, 64-bit. The own virtual camera needs Windows 11.

## Build

Visual Studio 2022 (C++), CMake 3.28+, Python 3.10 or 3.11 (for the model converter), and Inno
Setup 6.4+ for the installer.

```
build.bat            RelWithDebInfo build and the tests
build.bat release    Release build and the tests
build.bat package    Release build, the tests, and build\installer\RelyACamGestures-Setup-<ver>.exe
```

`build.bat` downloads Qt 6 and FFmpeg (obs-deps), ONNX Runtime and DirectML into `.deps/`, and
fetches Google's MediaPipe models into `models/`, converted to ONNX.

## How it fits with Rely-A-Cam

It's a separate program, so a crash in its vision models never takes OBS down. The plugin talks
to it over a local pipe and shared memory: [docs/LINK.md](docs/LINK.md).

## Licence

GPL-3.0-or-later ([LICENSE](LICENSE)). Third-party code, models and formats:
[docs/THIRD-PARTY.md](docs/THIRD-PARTY.md).
