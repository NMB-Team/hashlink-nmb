add_library(sdl.hdll
    sdl.c
    gl.c
)

if(NOT ANDROID AND NOT IOS_PLATFORM)
    target_sources(
        sdl.hdll
        PRIVATE
            gl_api.c
            gl_api.h
    )
endif()

option(WITH_SDL_ANGLE "Enable ANGLE support in sdl.hdll" OFF)
option(ANGLE_NMB_AUTO_DOWNLOAD "Automatically download the prebuilt ANGLE NMB package" ON)
set(
    ANGLE_NMB_ROOT
    ""
    CACHE PATH
    "Optional path to a manually extracted ANGLE NMB package"
)
set(
    ANGLE_NMB_MANIFEST_URL
    "https://github.com/NMB-Team/angle-nmb-builder/releases/download/nightly/latest.json"
    CACHE STRING
    "URL of the ANGLE NMB package manifest"
)
set(
    ANGLE_NMB_BUILDER_COMMIT
    ""
    CACHE STRING
    "Optional exact angle-nmb-builder commit to consume"
)
if(ANGLE_NMB_BUILDER_COMMIT STREQUAL "")
    file(
        STRINGS
        "${CMAKE_CURRENT_SOURCE_DIR}/../../other/angle-nmb-builder-commit.txt"
        ANGLE_NMB_BUILDER_COMMIT
        LIMIT_COUNT 1
    )
    string(STRIP "${ANGLE_NMB_BUILDER_COMMIT}" ANGLE_NMB_BUILDER_COMMIT)
    set(
        ANGLE_NMB_BUILDER_COMMIT
        "${ANGLE_NMB_BUILDER_COMMIT}"
        CACHE STRING
        "Optional exact angle-nmb-builder commit to consume"
        FORCE
    )
endif()
set(
    ANGLE_NMB_OFFLINE
    OFF
    CACHE BOOL
    "Disallow all ANGLE package network downloads"
)

if(WITH_SDL_ANGLE)
    include(FetchANGLENMB)
    angle_nmb_resolve_package()

    target_sources(
        sdl.hdll
        PRIVATE
            angle.c
            angle.h
    )
    target_link_libraries(
        sdl.hdll
        ANGLE_NMB::Headers
        ANGLE_NMB::EGL
        ANGLE_NMB::GLESv2
    )
    target_compile_definitions(
        sdl.hdll
        PRIVATE
            HL_SDL_HAS_ANGLE
            HL_ANGLE_REVISION="${ANGLE_NMB_RESOLVED_ANGLE_REVISION}"
    )
endif()

if ((NOT WIN32) AND DOWNLOAD_DEPENDENCIES)
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz
        URL_HASH SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7
        EXCLUDE_FROM_ALL
    )
    set(SDL_TEST OFF CACHE INTERNAL "")
    if (ANDROID)
        set(SDL_SHARED ON CACHE INTERNAL "")
        set(SDL_STATIC OFF CACHE INTERNAL "")
    else()
        set(SDL_STATIC ON CACHE INTERNAL "")
        set(SDL_SHARED OFF CACHE INTERNAL "")
        if (BUILD_SHARED_LIBS)
            set(SDL_STATIC_PIC ON CACHE INTERNAL "")
        endif()
    endif()
    FetchContent_MakeAvailable(SDL3)
else()
    if(WIN32)
        set(SDL3_DIR ${INCLUDES_BASE_DIR}/sdl/cmake)
        if(NOT MINGW)
            target_include_directories(sdl.hdll
                PRIVATE
                ${INCLUDES_BASE_DIR}/gl
            )
        endif()
    endif()

    find_package(SDL3 REQUIRED)
endif()

set(_WITH_SDL_VULKAN ON)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_WITH_SDL_VULKAN OFF)
endif()

set_as_hdll(sdl)
target_link_libraries(sdl.hdll
    libhl
    SDL3::SDL3
)
if(_WITH_SDL_VULKAN)
    target_sources(sdl.hdll PRIVATE vulkan.c)
    find_package(Vulkan REQUIRED)
    target_link_libraries(sdl.hdll Vulkan::Vulkan)
    add_library(shaderc.hdll shaderc.c)
    set_as_hdll(shaderc)
    target_include_directories(shaderc.hdll PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(shaderc.hdll libhl)
    if(MSVC)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_VULKAN_LIB_SUFFIX Lib32)
        else()
            set(_VULKAN_LIB_SUFFIX Lib)
        endif()
        find_library(SHADERC_LIBRARY
            NAMES shaderc_combined
            HINTS $ENV{VULKAN_SDK}
            PATH_SUFFIXES ${_VULKAN_LIB_SUFFIX} lib
        )
    elseif(NOT WIN32 AND NOT APPLE)
        find_library(SHADERC_LIBRARY
            NAMES shaderc_shared shaderc
        )
    endif()
    if(SHADERC_LIBRARY)
        target_link_libraries(shaderc.hdll ${SHADERC_LIBRARY})
        target_compile_definitions(shaderc.hdll PRIVATE HL_VULKAN_HAS_SHADERC)
    endif()
    install(
        TARGETS shaderc.hdll
        DESTINATION ${HDLL_DESTINATION}
    )
else()
    target_compile_definitions(sdl.hdll PRIVATE HL_DISABLE_SDL_VULKAN_NATIVE)
endif()
if(APPLE)
    target_compile_definitions(sdl.hdll PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(WITH_SDL_ANGLE)
    add_custom_command(
        TARGET sdl.hdll
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ANGLE_NMB::EGL>"
            "$<TARGET_FILE_DIR:sdl.hdll>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ANGLE_NMB::GLESv2>"
            "$<TARGET_FILE_DIR:sdl.hdll>"
    )

    if(APPLE)
        set_property(
            TARGET sdl.hdll
            PROPERTY BUILD_RPATH "@loader_path;@loader_path/../Frameworks"
        )
        set_property(
            TARGET sdl.hdll
            PROPERTY INSTALL_RPATH "@loader_path;@loader_path/../Frameworks"
        )
    elseif(UNIX)
        set_property(TARGET sdl.hdll PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET sdl.hdll PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    set_property(TARGET sdl.hdll PROPERTY INSTALL_RPATH_USE_LINK_PATH FALSE)

    if(WIN32)
        set(ANGLE_NMB_RUNTIME_INSTALL_DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        set(ANGLE_NMB_RUNTIME_INSTALL_DESTINATION ${HDLL_DESTINATION})
    endif()
    install(
        FILES
            "${ANGLE_NMB_EGL_RUNTIME}"
            "${ANGLE_NMB_GLES_RUNTIME}"
        DESTINATION ${ANGLE_NMB_RUNTIME_INSTALL_DESTINATION}
    )
endif()

if(WIN32)
    target_link_libraries(sdl.hdll
        winmm
        opengl32
    )

    option(WITH_SDL_DX12 "Build dx12.hdll for SDL windows." ${WITH_DX12})
    if(WITH_DX12 AND WITH_SDL_DX12 AND NOT TARGET dx12.hdll)
        add_library(dx12.hdll
            dx12.cpp
        )

        set_as_hdll(dx12)

        target_link_libraries(dx12.hdll
            libhl
            SDL3::SDL3
            d3d12
            dxgi
            dxcompiler
        )
        target_compile_definitions(dx12.hdll PRIVATE HL_DIRECTX_HAS_SDL)

        install(
            TARGETS
                dx12.hdll
            DESTINATION ${HDLL_DESTINATION}
        )
    endif()

    option(WITH_SDL_DIRECTX "Build directx.hdll for SDL windows." ON)
    if(WITH_SDL_DIRECTX AND NOT TARGET directx.hdll)
        add_library(directx.hdll
            ../directx/directx.cpp
            ../directx/gamecontroller.c
            ../directx/window.c
        )

        set_as_hdll(directx)

        target_link_libraries(directx.hdll
            libhl
            SDL3::SDL3
            d3d11
            dxgi
            d3dcompiler
            xinput9_1_0
            dinput8
        )
        target_compile_definitions(directx.hdll PRIVATE HL_DIRECTX_HAS_SDL)

        install(
            TARGETS
                directx.hdll
            DESTINATION ${HDLL_DESTINATION}
        )
    endif()
endif()

if(ANDROID)
    target_link_libraries(sdl.hdll GLESv3)
endif()

if((APPLE OR UNIX) AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
    target_include_directories(sdl.hdll
        PRIVATE
        ${OPENGL_INCLUDE_DIR}
    )
    target_link_libraries(sdl.hdll
        libhl
        ${OPENGL_gl_LIBRARY}
    )
endif()


install(
    TARGETS sdl.hdll
    DESTINATION ${HDLL_DESTINATION}
)
