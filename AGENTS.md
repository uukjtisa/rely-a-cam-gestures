# Rely-A-Cam-Gestures: rules for agents

## Layout

| Folder | What lives there |
|---|---|
| `src/app` | `main.cpp`, the `Controller` (one `Channel` per camera: the webcam, and each attached phone) |
| `src/vision` | camera input (Media Foundation, DirectShow through FFmpeg), the ONNX models, the `Detector` thread |
| `src/gestures` | the gesture library, itsgiving's reactions, Google's signs, your own, the engine, recorder and tester |
| `src/actions` | the action registry, filtered by situation (on its own / with OBS / inside Rely-A-Cam) |
| `src/framing` | zoom presets, pinch box, hold, follow face: the crop |
| `src/memes` | the meme library, media decoding (`Media`), and `Show` (what's on screen now) |
| `src/audio` | meme sound: the mixer (speakers, calls through VB-Cable, or the link) |
| `src/obs` | the OBS WebSocket v5 client (on its own) |
| `src/link` | LINK v1 to the Rely-A-Cam plugin (`docs/LINK.md`) |
| `src/output` | the picture pipeline, overlays, OBS Virtual Camera and the Windows 11 camera sinks |
| `src/vcam` | the Windows 11 virtual camera DLL (no Qt, no FFmpeg: Frame Server loads it) |
| `src/ui` | Qt Widgets: theme, kit, the main window, panels, the settings dialog |
| `tests/` | `racg-tests` (vision parity against MediaPipe, gestures, link wire format) |
| `installer/`, `scripts/` | Inno Setup script; packaging, model fetching, the icon |

## Rules

- **The picture shown is the picture sent.** Overlays and memes are drawn once in
  `output/pipeline.cpp`; the window and the outputs read the same frame.
- **Nothing blocks the UI thread on a device.** Camera, audio and link I/O live on their own
  threads; closing one is bounded (`Camera::close`, `kCloseWaitMs`) and Quit always ends the
  process (`kQuitGraceMs` in `main.cpp`).
- **LINK changes are versioned.** Change `docs/LINK.md`, `src/link/wire.hpp` and the plugin's
  `src/gestures/link.cpp` (in Rely-A-Cam) together, and bump the version on a breaking change.
- **The camera DLL stays dependency-free**: static CRT, no Qt, no FFmpeg. It runs inside a
  Windows service.
- **Colours come from `ui/theme.cpp`**; on the picture, from `output/overlay.cpp`. The dock and
  the phone copy the overlay's colours, so change all three together.
- Commit messages: `type(scope): Imperative summary`, then why, what, and what was verified.
