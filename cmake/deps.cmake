# Downloads and unpacks every prebuilt dependency into .deps/, each checked against a pinned hash.
#   obs-deps (FFmpeg) and obs-deps Qt 6: the same builds Rely-A-Cam's OBS plugin uses, so both read
#     the same Qt and FFmpeg (no Qt account, no installer, nothing registered on the machine).
#   ONNX Runtime 1.24.4 with DirectML (the last DirectML release) and DirectML 1.15.4, from NuGet.
# A zip already sitting in .deps/ with the right hash is used as is (no download).

set(RACG_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Where prebuilt dependencies are unpacked")

function(racg_fetch name url sha256 dir)
  set(zip "${RACG_DEPS_DIR}/${name}")
  set(out "${RACG_DEPS_DIR}/${dir}")
  if(EXISTS "${out}/.racg-ok")
    return()
  endif()
  if(EXISTS "${zip}")
    file(SHA256 "${zip}" have)
    if(NOT have STREQUAL "${sha256}")
      file(REMOVE "${zip}")
    endif()
  endif()
  if(NOT EXISTS "${zip}")
    message(STATUS "Downloading ${name}")
    file(DOWNLOAD "${url}" "${zip}" EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS STATUS st)
    list(GET st 0 code)
    if(NOT code EQUAL 0)
      list(GET st 1 msg)
      message(FATAL_ERROR "Download of ${name} failed: ${msg}")
    endif()
  endif()
  message(STATUS "Unpacking ${name}")
  file(REMOVE_RECURSE "${out}")
  file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${out}")
  file(TOUCH "${out}/.racg-ok")
endfunction()

set(_obs "https://github.com/obsproject/obs-deps/releases/download/2025-07-11")
racg_fetch(windows-deps-2025-07-11-x64.zip "${_obs}/windows-deps-2025-07-11-x64.zip"
  c8c642c1070dc31ce9a0f1e4cef5bb992f4bff4882255788b5da12129e85caa7 obs-deps)
racg_fetch(windows-deps-qt6-2025-07-11-x64.zip "${_obs}/windows-deps-qt6-2025-07-11-x64.zip"
  0e76bf0555dd5382838850b748d3dcfab44a1e1058441309ab54e1a65b156d0a qt6)

set(_nuget "https://api.nuget.org/v3-flatcontainer")
racg_fetch(microsoft.ml.onnxruntime.directml.1.24.4.nupkg
  "${_nuget}/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg"
  57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681 onnxruntime)
racg_fetch(microsoft.ai.directml.1.15.4.nupkg
  "${_nuget}/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg"
  4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9 directml)

# --- Qt 6 -----------------------------------------------------------------------------------------
set(RACG_QT_DIR "${RACG_DEPS_DIR}/qt6")
list(PREPEND CMAKE_PREFIX_PATH "${RACG_QT_DIR}")

# --- FFmpeg (from obs-deps) -------------------------------------------------------------------------
set(_od "${RACG_DEPS_DIR}/obs-deps")
foreach(lib avcodec avdevice avformat avutil swscale swresample)
  add_library(FFmpeg::${lib} SHARED IMPORTED GLOBAL)
  file(GLOB _dll "${_od}/bin/${lib}-*.dll")
  set_target_properties(FFmpeg::${lib} PROPERTIES
    IMPORTED_IMPLIB "${_od}/lib/${lib}.lib"
    IMPORTED_LOCATION "${_dll}"
    INTERFACE_INCLUDE_DIRECTORIES "${_od}/include")
endforeach()

# --- ONNX Runtime + DirectML ---------------------------------------------------------------------
set(_ort "${RACG_DEPS_DIR}/onnxruntime")
add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
  IMPORTED_IMPLIB "${_ort}/runtimes/win-x64/native/onnxruntime.lib"
  IMPORTED_LOCATION "${_ort}/runtimes/win-x64/native/onnxruntime.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${_ort}/build/native/include")
set(RACG_DIRECTML_DLL "${RACG_DEPS_DIR}/directml/bin/x64-win/DirectML.dll")

# Every runtime DLL the exe needs beside it (copied after each build and by the installer).
file(GLOB RACG_FFMPEG_DLLS "${_od}/bin/avcodec-*.dll" "${_od}/bin/avdevice-*.dll" "${_od}/bin/avfilter-*.dll" "${_od}/bin/avformat-*.dll" "${_od}/bin/avutil-*.dll"
  "${_od}/bin/swscale-*.dll" "${_od}/bin/swresample-*.dll" "${_od}/bin/libx264-*.dll" "${_od}/bin/zlib.dll"
  "${_od}/bin/libcurl.dll" "${_od}/bin/librist.dll" "${_od}/bin/srt.dll" "${_od}/bin/datachannel.dll")
set(RACG_RUNTIME_DLLS
  "${_ort}/runtimes/win-x64/native/onnxruntime.dll"
  "${_ort}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
  "${RACG_DIRECTML_DLL}"
  ${RACG_FFMPEG_DLLS})
