cmake_minimum_required(VERSION 3.24)

project(EinzigViewer VERSION 1.0.0 LANGUAGES CXX RC)
configure_file(src/AppVersion.h.in generated/AppVersion.h @ONLY)

add_library(einzig_fingerprint STATIC src/ImageFingerprint.cpp)
target_compile_features(einzig_fingerprint PUBLIC cxx_std_20)
target_include_directories(einzig_fingerprint PUBLIC src)
target_compile_definitions(einzig_fingerprint PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(einzig_fingerprint PRIVATE bcrypt)
if(MSVC)
    target_compile_options(einzig_fingerprint PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_library(einzig_ai STATIC src/AiRuntime.cpp src/AiManager.cpp src/AiResources.cpp src/AiSettings.cpp)
target_compile_features(einzig_ai PUBLIC cxx_std_20)
target_include_directories(einzig_ai PUBLIC src)
target_compile_definitions(einzig_ai PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(einzig_ai PRIVATE windowscodecs ole32 bcrypt dxgi einzig_fingerprint)
if(MSVC)
    target_compile_options(einzig_ai PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(EinzigViewerUpdateProbe tests/UpdateProbe.cpp src/UpdateChecker.cpp resources/app.manifest)
target_compile_features(EinzigViewerUpdateProbe PRIVATE cxx_std_20)
target_link_libraries(EinzigViewerUpdateProbe PRIVATE winhttp windowsapp comctl32)
target_include_directories(EinzigViewerUpdateProbe PRIVATE src)
target_compile_definitions(EinzigViewerUpdateProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(EinzigViewerUpdateProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_library(einzig_miniz STATIC third_party/miniz/miniz.c
    third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_zip.c)
set_source_files_properties(third_party/miniz/miniz.c third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_tdef.c third_party/miniz/miniz_zip.c PROPERTIES LANGUAGE CXX)
target_include_directories(einzig_miniz PUBLIC third_party/miniz)
target_compile_definitions(einzig_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(EinzigViewerRotationProbe tests/RotationProbe.cpp src/ImageRotation.cpp)
target_include_directories(EinzigViewerRotationProbe PRIVATE src)
if(MSVC)
    target_compile_options(EinzigViewerRotationProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(EinzigViewerCompositionProbe tests/CompositionProbe.cpp src/ImageComposition.cpp)
target_include_directories(EinzigViewerCompositionProbe PRIVATE src)
target_compile_definitions(EinzigViewerCompositionProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(EinzigViewerCompositionProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(EinzigViewer WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/AppUpdates.cpp
    src/AppRotation.cpp
    src/AppClipboard.cpp
    src/AppAi.cpp
    src/AiResourceDialog.cpp
    resources/AiResourceDialog.rc
    src/ImageClipboard.cpp
    src/ImageComposition.cpp
    src/ImageRotation.cpp
    src/UpdateChecker.cpp
    src/UpdateChecker.h
    src/FolderModel.cpp
    src/FolderModel.h
    src/EpubBook.cpp
    src/EpubBook.h
    src/ContinuousLayout.h
    src/ContinuousReader.cpp
    src/ImageCache.cpp
    src/ImageCache.h
    src/ImageData.h
    src/WicDecoder.cpp
    src/WicDecoder.h
    src/ImageExporter.cpp
    src/ImageExporter.h
    src/EnhancementWorker.cpp
    src/EnhancementWorker.h
    src/ImageEnhancer.h
    src/NullImageEnhancer.cpp
    src/NullImageEnhancer.h
    resources/EinzigViewer.rc
    resources/app.manifest
    resources/icon/EinzigViewer.ico
    resources/icon/EinzigViewerIcon.png
)

target_compile_definitions(EinzigViewer PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_include_directories(EinzigViewer PRIVATE "${CMAKE_BINARY_DIR}/generated")

if(MSVC)
    target_compile_options(EinzigViewer PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(EinzigViewer PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(EinzigViewer PRIVATE
    d2d1
    dwrite
    windowscodecs
    shlwapi
    shell32
    ole32
    comdlg32
    user32
    gdi32
    xmllite
    einzig_miniz
    winhttp
    windowsapp
    comctl32
    einzig_ai
)

set(EINZIGVIEWER_RTX_SDK_ROOT "${CMAKE_SOURCE_DIR}/.sdk/rtx-video-1.1.0"
    CACHE PATH "Path to NVIDIA RTX Video SDK 1.1 root")

if(EXISTS "${EINZIGVIEWER_RTX_SDK_ROOT}/include/nvsdk_ngx_helpers_vsr.h"
   AND EXISTS "${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib")
    message(STATUS "EinzigViewer RTX backend: enabled (${EINZIGVIEWER_RTX_SDK_ROOT})")
    target_sources(EinzigViewer PRIVATE
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/RtxHdrPresenter.cpp
        src/RtxHdrPresenter.h
    )
    target_compile_definitions(EinzigViewer PRIVATE EINZIGVIEWER_WITH_RTX=1)
    target_include_directories(EinzigViewer SYSTEM PRIVATE "${EINZIGVIEWER_RTX_SDK_ROOT}/include")
    target_link_libraries(EinzigViewer PRIVATE
        d3d11
        dxgi
        "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    add_custom_command(TARGET EinzigViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:EinzigViewer>/nvngx_vsr.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:EinzigViewer>/nvngx_truehdr.dll"
        VERBATIM
    )

    add_executable(EinzigViewerRtxProbe
        tests/RtxProbe.cpp
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
        src/ImageEnhancer.h
        src/ImageData.h
    )
    target_compile_definitions(EinzigViewerRtxProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX EINZIGVIEWER_WITH_RTX=1)
    target_include_directories(EinzigViewerRtxProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(EinzigViewerRtxProbe SYSTEM PRIVATE "${EINZIGVIEWER_RTX_SDK_ROOT}/include")
    target_link_libraries(EinzigViewerRtxProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(EinzigViewerRtxProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET EinzigViewerRtxProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:EinzigViewerRtxProbe>/nvngx_vsr.dll"
        VERBATIM
    )

    add_executable(EinzigViewerHdrProbe
        tests/HdrProbe.cpp
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/ImageData.h
    )
    target_compile_definitions(EinzigViewerHdrProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX EINZIGVIEWER_WITH_RTX=1)
    target_include_directories(EinzigViewerHdrProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(EinzigViewerHdrProbe SYSTEM PRIVATE "${EINZIGVIEWER_RTX_SDK_ROOT}/include")
    target_link_libraries(EinzigViewerHdrProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EINZIGVIEWER_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(EinzigViewerHdrProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET EinzigViewerHdrProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${EINZIGVIEWER_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:EinzigViewerHdrProbe>/nvngx_truehdr.dll"
        VERBATIM
    )
else()
    message(STATUS "EinzigViewer RTX backend: disabled (set EINZIGVIEWER_RTX_SDK_ROOT)")
    target_compile_definitions(EinzigViewer PRIVATE EINZIGVIEWER_WITH_RTX=0)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT EinzigViewer)

add_executable(EinzigViewerReaderProbe tests/ReaderProbe.cpp src/EpubBook.cpp src/FolderModel.cpp
    src/WicDecoder.cpp src/ImageExporter.cpp src/ImageCache.cpp src/ImageRotation.cpp)
target_include_directories(EinzigViewerReaderProbe PRIVATE src)
target_compile_definitions(EinzigViewerReaderProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(EinzigViewerReaderProbe PRIVATE einzig_miniz xmllite windowscodecs ole32 shlwapi user32 winhttp windowsapp)
if(MSVC)
    target_compile_options(EinzigViewerReaderProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
enable_testing()
add_test(NAME UpdateProbe COMMAND EinzigViewerUpdateProbe)
add_test(NAME RotationProbe COMMAND EinzigViewerRotationProbe)
add_test(NAME CompositionProbe COMMAND EinzigViewerCompositionProbe)
add_executable(EinzigViewerClipboardProbe tests/ClipboardProbe.cpp src/ImageClipboard.cpp)
target_include_directories(EinzigViewerClipboardProbe PRIVATE src)
target_compile_definitions(EinzigViewerClipboardProbe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(EinzigViewerClipboardProbe PRIVATE windowscodecs ole32 user32)
if(MSVC)
    target_compile_options(EinzigViewerClipboardProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_test(NAME ClipboardProbe COMMAND EinzigViewerClipboardProbe)
set_tests_properties(UpdateProbe PROPERTIES TIMEOUT 40)
get_target_property(reader_ui_sources EinzigViewer SOURCES)
list(REMOVE_ITEM reader_ui_sources src/main.cpp)
add_executable(EinzigViewerReaderUiProbe tests/ReaderUiProbe.cpp ${reader_ui_sources})
get_target_property(reader_ui_libraries EinzigViewer LINK_LIBRARIES)
get_target_property(reader_ui_definitions EinzigViewer COMPILE_DEFINITIONS)
get_target_property(reader_ui_includes EinzigViewer INCLUDE_DIRECTORIES)
target_link_libraries(EinzigViewerReaderUiProbe PRIVATE ${reader_ui_libraries})
target_compile_definitions(EinzigViewerReaderUiProbe PRIVATE ${reader_ui_definitions} EINZIGVIEWER_TESTING=1)
target_include_directories(EinzigViewerReaderUiProbe PRIVATE src)
if(reader_ui_includes)
    target_include_directories(EinzigViewerReaderUiProbe SYSTEM PRIVATE ${reader_ui_includes})
endif()
if(MSVC)
    target_compile_options(EinzigViewerReaderUiProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(EinzigViewerReaderUiProbe PRIVATE /MANIFEST:NO)
endif()
add_test(NAME ReaderProbe COMMAND EinzigViewerReaderProbe "${CMAKE_BINARY_DIR}/reader-probe")
add_test(NAME ImageIoProbe COMMAND EinzigViewerImageIoProbe "${CMAKE_BINARY_DIR}/image-io-probe")
add_test(NAME ImageExporterComProbe COMMAND EinzigViewerImageIoProbe --uninitialized-export "${CMAKE_BINARY_DIR}/exporter-com-probe/fresh.png")
add_test(NAME ReaderUiProbe COMMAND EinzigViewerReaderUiProbe "${CMAKE_BINARY_DIR}/reader-probe")
set_tests_properties(ReaderUiProbe PROPERTIES DEPENDS ReaderProbe TIMEOUT 120)

add_executable(EinzigViewerUpdateUiProbe tests/UpdateUiProbe.cpp ${reader_ui_sources})
target_link_libraries(EinzigViewerUpdateUiProbe PRIVATE ${reader_ui_libraries})
target_compile_definitions(EinzigViewerUpdateUiProbe PRIVATE ${reader_ui_definitions} EINZIGVIEWER_TESTING=1)
target_include_directories(EinzigViewerUpdateUiProbe PRIVATE src)
if(reader_ui_includes)
    target_include_directories(EinzigViewerUpdateUiProbe SYSTEM PRIVATE ${reader_ui_includes})
endif()
if(MSVC)
    target_compile_options(EinzigViewerUpdateUiProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(EinzigViewerUpdateUiProbe PRIVATE /MANIFEST:NO)
endif()
set_target_properties(EinzigViewerUpdateUiProbe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/update-ui-probe/$<CONFIG>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/update-ui-probe/$<CONFIG>/portable.ini" CONTENT "[portable]\n")
add_test(NAME UpdateUiProbe COMMAND EinzigViewerUpdateUiProbe)
set_tests_properties(UpdateUiProbe PROPERTIES TIMEOUT 60)

add_executable(EinzigViewerAiUiProbe tests/AiUiProbe.cpp ${reader_ui_sources})
target_link_libraries(EinzigViewerAiUiProbe PRIVATE ${reader_ui_libraries})
target_compile_definitions(EinzigViewerAiUiProbe PRIVATE ${reader_ui_definitions} EINZIGVIEWER_TESTING=1)
target_include_directories(EinzigViewerAiUiProbe PRIVATE src)
if(reader_ui_includes)
    target_include_directories(EinzigViewerAiUiProbe SYSTEM PRIVATE ${reader_ui_includes})
endif()
if(MSVC)
    target_compile_options(EinzigViewerAiUiProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(EinzigViewerAiUiProbe PRIVATE /MANIFEST:NO)
endif()
set_target_properties(EinzigViewerAiUiProbe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ai-ui-probe/$<CONFIG>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/ai-ui-probe/$<CONFIG>/portable.ini" CONTENT "[portable]\n")
add_test(NAME AiUiProbe COMMAND EinzigViewerAiUiProbe "${CMAKE_BINARY_DIR}/reader-probe")
set_tests_properties(AiUiProbe PROPERTIES DEPENDS ReaderProbe TIMEOUT 90)

add_executable(EinzigViewerAiFakeProcess tests/AiFakeProcess.cpp src/WicDecoder.cpp src/ImageExporter.cpp)
target_include_directories(EinzigViewerAiFakeProcess PRIVATE src)
target_compile_definitions(EinzigViewerAiFakeProcess PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(EinzigViewerAiFakeProcess PRIVATE windowscodecs ole32)
target_link_libraries(EinzigViewerAiFakeProcess PRIVATE einzig_fingerprint)
add_executable(EinzigViewerAiRuntimeProbe tests/AiRuntimeProbe.cpp src/WicDecoder.cpp src/ImageExporter.cpp)
target_link_libraries(EinzigViewerAiRuntimeProbe PRIVATE einzig_ai)
add_executable(EinzigViewerAiManagerProbe tests/AiManagerProbe.cpp src/WicDecoder.cpp src/ImageExporter.cpp src/ImageRotation.cpp)
target_link_libraries(EinzigViewerAiManagerProbe PRIVATE einzig_ai user32)
add_executable(EinzigViewerAiResourcesProbe tests/AiResourcesProbe.cpp)
target_link_libraries(EinzigViewerAiResourcesProbe PRIVATE einzig_ai)
add_executable(EinzigViewerAiResourceDialogProbe tests/AiResourceDialogProbe.cpp
    src/AiResourceDialog.cpp resources/AiResourceDialog.rc resources/app.manifest)
target_link_libraries(EinzigViewerAiResourceDialogProbe PRIVATE einzig_ai user32 comctl32)
if(MSVC)
    foreach(ai_target EinzigViewerAiFakeProcess EinzigViewerAiRuntimeProbe EinzigViewerAiManagerProbe EinzigViewerAiResourcesProbe EinzigViewerAiResourceDialogProbe)
        target_compile_options(${ai_target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endforeach()
endif()
add_test(NAME AiRuntimeProbe COMMAND EinzigViewerAiRuntimeProbe "$<TARGET_FILE:EinzigViewerAiFakeProcess>")
add_test(NAME AiManagerProbe COMMAND EinzigViewerAiManagerProbe "${CMAKE_BINARY_DIR}/ai-manager-probe")
add_test(NAME AiResourcesProbe COMMAND EinzigViewerAiResourcesProbe)
add_test(NAME AiResourceDialogProbe COMMAND EinzigViewerAiResourceDialogProbe "${CMAKE_BINARY_DIR}/ai-resource-dialog-probe")
set_tests_properties(AiResourceDialogProbe PROPERTIES TIMEOUT 60)
set_tests_properties(AiRuntimeProbe AiManagerProbe PROPERTIES TIMEOUT 120)

set_target_properties(EinzigViewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out"
)

add_executable(EinzigViewerImageIoProbe
    tests/ImageIoProbe.cpp
    src/ImageRotation.cpp
    src/WicDecoder.cpp
    src/WicDecoder.h
    src/ImageExporter.cpp
    src/ImageExporter.h
    src/ImageData.h
)
target_compile_definitions(EinzigViewerImageIoProbe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(EinzigViewerImageIoProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(EinzigViewerImageIoProbe PRIVATE windowscodecs ole32 einzig_fingerprint)
target_link_libraries(EinzigViewerReaderProbe PRIVATE einzig_fingerprint)
if(MSVC)
    target_compile_options(EinzigViewerImageIoProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()


find_program(EINZIGVIEWER_TEST_NODE node)
if(EINZIGVIEWER_TEST_NODE)
    add_test(NAME ReleaseProfileProbe COMMAND "${EINZIGVIEWER_TEST_NODE}" "${CMAKE_SOURCE_DIR}/tests/ReleaseProfileProbe.mjs")
endif()
