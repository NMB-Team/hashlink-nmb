add_library(sdl.hdll
    sdl.c
    gl.c
)

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
    if(NOT APPLE)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set(_VULKAN_LIB_SUFFIX Lib32)
        else()
            set(_VULKAN_LIB_SUFFIX Lib)
        endif()
        find_library(SHADERC_LIBRARY
            NAMES shaderc_shared shaderc
            HINTS $ENV{VULKAN_SDK}
            PATH_SUFFIXES ${_VULKAN_LIB_SUFFIX} lib
        )
    endif()
    if(SHADERC_LIBRARY)
        target_link_libraries(sdl.hdll ${SHADERC_LIBRARY})
        target_compile_definitions(sdl.hdll PRIVATE HL_VULKAN_HAS_SHADERC)
        if(WIN32)
            find_file(SHADERC_RUNTIME_LIBRARY
                NAMES shaderc_shared.dll
                HINTS $ENV{VULKAN_SDK}
                PATH_SUFFIXES Bin
            )
        endif()
    endif()
else()
    target_compile_definitions(sdl.hdll PRIVATE HL_DISABLE_SDL_VULKAN_NATIVE)
endif()
if(APPLE)
    target_compile_definitions(sdl.hdll PRIVATE GL_SILENCE_DEPRECATION)
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
if(SHADERC_RUNTIME_LIBRARY)
    install(
        FILES ${SHADERC_RUNTIME_LIBRARY}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
