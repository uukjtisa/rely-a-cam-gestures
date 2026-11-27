# Third-party code, models and formats

Rely-A-Cam-Gestures is GPL-3.0-or-later (`LICENSE`). It ships, ports or follows the work below.
Each keeps its own licence.

## Shipped beside the app

| What | Licence | Where it comes from |
|---|---|---|
| Qt 6.8 (Core, Gui, Widgets, Network, Svg, and their plugins) | LGPL-3.0, linked dynamically | The Qt Company, as built by obs-deps |
| FFmpeg 7.1 (avcodec, avdevice, avfilter, avformat, avutil, swresample, swscale) | GPL-2.0-or-later as built (it includes libx264) | ffmpeg.org, as built by obs-deps |
| libx264 | GPL-2.0-or-later | VideoLAN, through obs-deps' FFmpeg |
| libvpx (VP8/VP9 decoding, used for WebM memes with transparency) | BSD-3-Clause | WebM Project, through obs-deps' FFmpeg |
| librist, SRT, libdatachannel, libcurl, zlib | BSD-2-Clause, MPL-2.0, MPL-2.0, curl, zlib | through obs-deps' FFmpeg |
| ONNX Runtime 1.24 | MIT | Microsoft, the official release |
| DirectML | Microsoft DirectML licence (redistributable) | Microsoft, with ONNX Runtime's DirectML package |
| Microsoft C++ runtime (msvcp140, vcruntime140, concrt140) | Visual Studio redistributable terms | Microsoft Visual Studio |

## Models

The hand, face, pose and gesture models are Google's MediaPipe models (Apache-2.0), fetched from
Google's own storage by `scripts/fetch_models.py`, checked against pinned hashes, and converted to
ONNX. `models/manifest.json` records each file's source and hash.

## Ported or followed

| What | Licence | How it's used |
|---|---|---|
| itsgiving v2 (github.com/itsgiving) | MIT | Its 14 face and body reactions and its calibration file format, ported line by line to C++ (`src/gestures/itsgiving.*`) |
| OBS Studio's virtual camera shared-memory queue | GPL-2.0-or-later | The queue layout OBS Virtual Camera reads, written by `src/output/obs_camera.*` so the picture shows up as "OBS Virtual Camera". No OBS code is copied |
| Microsoft's Windows 11 virtual camera sample (Windows-Camera repository) | MIT | The shape of the media source, stream and activation objects in `src/vcam/` |
