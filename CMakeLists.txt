cmake_minimum_required(VERSION 3.24)

project(EinzigViewer VERSION 0.6.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(EinzigViewer WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/FolderModel.cpp
    src/FolderModel.h
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

set_target_properties(EinzigViewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out"
)

add_executable(EinzigViewerImageIoProbe
    tests/ImageIoProbe.cpp
    src/WicDecoder.cpp
    src/WicDecoder.h
    src/ImageExporter.cpp
    src/ImageExporter.h
    src/ImageData.h
)
target_compile_definitions(EinzigViewerImageIoProbe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(EinzigViewerImageIoProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(EinzigViewerImageIoProbe PRIVATE windowscodecs ole32)
if(MSVC)
    target_compile_options(EinzigViewerImageIoProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
