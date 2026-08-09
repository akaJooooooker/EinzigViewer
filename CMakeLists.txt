cmake_minimum_required(VERSION 3.24)

project(Eidolyx VERSION 0.5.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Eidolyx WIN32
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
    src/EnhancementWorker.cpp
    src/EnhancementWorker.h
    src/ImageEnhancer.h
    src/NullImageEnhancer.cpp
    src/NullImageEnhancer.h
    resources/Eidolyx.rc
    resources/app.manifest
    resources/icon/Eidolyx.ico
    resources/icon/EidolyxIcon.png
)

target_compile_definitions(Eidolyx PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(Eidolyx PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(Eidolyx PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(Eidolyx PRIVATE
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

set(EIDOLYX_RTX_SDK_ROOT "${CMAKE_SOURCE_DIR}/.sdk/rtx-video-1.1.0"
    CACHE PATH "Path to NVIDIA RTX Video SDK 1.1 root")

if(EXISTS "${EIDOLYX_RTX_SDK_ROOT}/include/nvsdk_ngx_helpers_vsr.h"
   AND EXISTS "${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib")
    message(STATUS "Eidolyx RTX backend: enabled (${EIDOLYX_RTX_SDK_ROOT})")
    target_sources(Eidolyx PRIVATE
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/RtxHdrPresenter.cpp
        src/RtxHdrPresenter.h
    )
    target_compile_definitions(Eidolyx PRIVATE EIDOLYX_WITH_RTX=1)
    target_include_directories(Eidolyx SYSTEM PRIVATE "${EIDOLYX_RTX_SDK_ROOT}/include")
    target_link_libraries(Eidolyx PRIVATE
        d3d11
        dxgi
        "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    add_custom_command(TARGET Eidolyx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:Eidolyx>/nvngx_vsr.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:Eidolyx>/nvngx_truehdr.dll"
        VERBATIM
    )

    add_executable(EidolyxRtxProbe
        tests/RtxProbe.cpp
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
        src/ImageEnhancer.h
        src/ImageData.h
    )
    target_compile_definitions(EidolyxRtxProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX EIDOLYX_WITH_RTX=1)
    target_include_directories(EidolyxRtxProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(EidolyxRtxProbe SYSTEM PRIVATE "${EIDOLYX_RTX_SDK_ROOT}/include")
    target_link_libraries(EidolyxRtxProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(EidolyxRtxProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET EidolyxRtxProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:EidolyxRtxProbe>/nvngx_vsr.dll"
        VERBATIM
    )

    add_executable(EidolyxHdrProbe
        tests/HdrProbe.cpp
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/ImageData.h
    )
    target_compile_definitions(EidolyxHdrProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX EIDOLYX_WITH_RTX=1)
    target_include_directories(EidolyxHdrProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(EidolyxHdrProbe SYSTEM PRIVATE "${EIDOLYX_RTX_SDK_ROOT}/include")
    target_link_libraries(EidolyxHdrProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${EIDOLYX_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(EidolyxHdrProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET EidolyxHdrProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${EIDOLYX_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:EidolyxHdrProbe>/nvngx_truehdr.dll"
        VERBATIM
    )
else()
    message(STATUS "Eidolyx RTX backend: disabled (set EIDOLYX_RTX_SDK_ROOT)")
    target_compile_definitions(Eidolyx PRIVATE EIDOLYX_WITH_RTX=0)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Eidolyx)

set_target_properties(Eidolyx PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out"
)
