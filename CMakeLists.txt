cmake_minimum_required(VERSION 3.28)
project(rely-a-cam-gestures VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Rely-A-Cam-Gestures builds for 64-bit Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(cmake/deps.cmake)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

add_compile_options(/W4 /permissive- /utf-8 /MP /Zc:__cplusplus /wd4251)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN QT_NO_KEYWORDS
  RACG_VERSION="${PROJECT_VERSION}")

# --- racg_core: everything that sees, decides and acts; no widgets. The exe and the tests link it.
add_library(racg_core STATIC
  src/actions/registry.cpp
  src/actions/runner.cpp
  src/app/controller.cpp
  src/audio/sound.cpp
  src/common/autostart.cpp
  src/common/log.cpp
  src/common/paths.cpp
  src/common/settings.cpp
  src/gestures/engine.cpp
  src/gestures/itsgiving.cpp
  src/gestures/library.cpp
  src/gestures/matcher.cpp
  src/gestures/sessions.cpp
  src/framing/framer.cpp
  src/link/server.cpp
  src/memes/library.cpp
  src/memes/media.cpp
  src/memes/show.cpp
  src/obs/websocket.cpp
  src/output/obs_camera.cpp
  src/output/overlay.cpp
  src/output/pipeline.cpp
  src/output/win_camera.cpp
  src/vision/camera.cpp
  src/vision/crop.cpp
  src/vision/landmarkers.cpp
  src/vision/observation.cpp
  src/vision/onnx.cpp
  src/vision/detector.cpp
  src/vision/ssd.cpp
)
target_include_directories(racg_core PUBLIC src)
target_link_libraries(racg_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network onnxruntime
  FFmpeg::avcodec FFmpeg::avdevice FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample
  mfplat mf mfreadwrite mfuuid strmiids ole32 crypt32 advapi32)
# A developer build finds the models in the source tree; an installed one beside the exe.
target_compile_definitions(racg_core PRIVATE RACG_DEV_MODELS="${CMAKE_SOURCE_DIR}/models")

# --- the app ------------------------------------------------------------------------------------
add_executable(rely-a-cam-gestures WIN32
  src/app/main.cpp
  resources/app.rc
  src/ui/theme.cpp
  src/ui/widgets/kit.cpp
  src/ui/widgets/preview.cpp
  src/ui/panels/library_panel.cpp
  src/ui/panels/editor_panel.cpp
  src/ui/dialogs/settings_dialog.cpp
  src/ui/windows/main_window.cpp
  resources/resources.qrc
)
target_link_libraries(rely-a-cam-gestures PRIVATE racg_core Qt6::Widgets Qt6::Svg)
set_target_properties(rely-a-cam-gestures PROPERTIES OUTPUT_NAME "Rely-A-Cam-Gestures")

# --- the "Rely-A-Cam Gestures" camera (Windows 11): a media source DLL the Frame Server loads.
# No Qt and no FFmpeg: it runs inside a Windows service. The installer registers it (regsvr32).
add_library(racg-vcam SHARED src/vcam/source.cpp src/vcam/vcam.def)
target_include_directories(racg-vcam PRIVATE src)
target_link_libraries(racg-vcam PRIVATE mfplat mfuuid ole32 advapi32 runtimeobject)
set_target_properties(racg-vcam PROPERTIES OUTPUT_NAME "Rely-A-Cam-Gestures-Camera"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_dependencies(rely-a-cam-gestures racg-vcam)

# --- tests ----------------------------------------------------------------------------------------
enable_testing()
add_executable(racg-tests
  tests/main.cpp
  tests/gestures/gestures_test.cpp
  tests/vision/vision_test.cpp
)
target_link_libraries(racg-tests PRIVATE racg_core)
target_compile_definitions(racg-tests PRIVATE
  RACG_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data" RACG_MODELS="${CMAKE_SOURCE_DIR}/models")
add_test(NAME racg-tests COMMAND racg-tests)

# Runtime DLLs beside every executable, and the Qt plugin folders Qt looks for beside the app
# (<app>/platforms, <app>/imageformats, ...). The installer ships the same set.
set(RACG_QT_PLUGIN_DIRS platforms imageformats styles iconengines tls)
function(racg_copy_runtime target)
  set(cmds)
  foreach(p ${RACG_QT_PLUGIN_DIRS})
    list(APPEND cmds COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
      "${RACG_QT_DIR}/plugins/${p}" "$<TARGET_FILE_DIR:${target}>/${p}")
  endforeach()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RACG_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Qt6::Core> $<TARGET_FILE:Qt6::Gui> $<TARGET_FILE:Qt6::Widgets> $<TARGET_FILE:Qt6::Network>
      $<TARGET_FILE:Qt6::Svg> $<TARGET_FILE_DIR:${target}>
    ${cmds}
    COMMAND_EXPAND_LISTS)
endfunction()
racg_copy_runtime(racg-tests)
racg_copy_runtime(rely-a-cam-gestures)

# The installer (build.bat package): stages the build and runs Inno Setup (scripts/package.ps1).
add_custom_target(package-installer
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/package.ps1"
    -BuildDir "${CMAKE_BINARY_DIR}" -Config $<CONFIG>
  DEPENDS rely-a-cam-gestures racg-vcam
  USES_TERMINAL
  VERBATIM)
