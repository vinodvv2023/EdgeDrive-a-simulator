cmake_minimum_required(VERSION 3.20)
project(Assistant LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)

# ========================================
# STT Service (Whisper.cpp)
# ========================================
# Enable CUDA for Whisper
set(GGML_CUDA ON CACHE BOOL "Enable CUDA for Whisper")
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Disable Whisper examples")
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Disable Whisper tests")
add_subdirectory(${EXTERNAL_DIR}/whisper.cpp)

add_executable(stt_service stt_service.cpp)
target_include_directories(stt_service PRIVATE 
    ${EXTERNAL_DIR}/cpp-httplib 
    ${EXTERNAL_DIR}/whisper.cpp/include 
    ${EXTERNAL_DIR}/whisper.cpp/ggml/include
)
target_link_libraries(stt_service PRIVATE whisper)

# ========================================
# TTS Service (Windows SAPI)
# ========================================
add_executable(tts_service tts_service.cpp)
target_include_directories(tts_service PRIVATE ${EXTERNAL_DIR}/cpp-httplib)
target_link_libraries(tts_service PRIVATE sapi.lib ole32.lib)

# ========================================
# Assistant GUI Simulator
# ========================================
set(IMGUI_DIR ${EXTERNAL_DIR}/imgui)
add_executable(assistant_gui 
    assistant_gui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(assistant_gui PRIVATE 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
    ${EXTERNAL_DIR}/miniaudio
    ${EXTERNAL_DIR}/cpp-httplib
    ${EXTERNAL_DIR}/sherpa-onnx/include
)

# Link sherpa-onnx libs and DX11 libs
target_link_libraries(assistant_gui PRIVATE 
    d3d11.lib d3dcompiler.lib dxgi.lib
    ${EXTERNAL_DIR}/sherpa-onnx/lib/sherpa-onnx-cxx-api.lib
    ${EXTERNAL_DIR}/sherpa-onnx/lib/sherpa-onnx-c-api.lib
    ${EXTERNAL_DIR}/sherpa-onnx/lib/onnxruntime.lib
)

# Copy sherpa-onnx DLLs to output dir so the exe can run
add_custom_command(TARGET assistant_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${EXTERNAL_DIR}/sherpa-onnx/lib/onnxruntime.dll"
    $<TARGET_FILE_DIR:assistant_gui>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${EXTERNAL_DIR}/sherpa-onnx/lib/onnxruntime_providers_shared.dll"
    $<TARGET_FILE_DIR:assistant_gui>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${EXTERNAL_DIR}/sherpa-onnx/lib/sherpa-onnx-cxx-api.dll"
    $<TARGET_FILE_DIR:assistant_gui>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${EXTERNAL_DIR}/sherpa-onnx/lib/sherpa-onnx-c-api.dll"
    $<TARGET_FILE_DIR:assistant_gui>
)