add_library(sdl.hdll
    sdl.c
    gl.c
)

if ((NOT WIN32) AND DOWNLOAD_DEPENDENCIES)
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.6/SDL3-3.4.6.tar.gz
        URL_HASH SHA256=96bdd3f0c442105c87c287d213858b2df8207c87d54991dd589078c3746cb386
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

set_as_hdll(sdl)
target_link_libraries(sdl.hdll
    libhl
    SDL3::SDL3
)
if(APPLE)
    target_compile_definitions(sdl.hdll PRIVATE GL_SILENCE_DEPRECATION)
endif()

if(WIN32)
    target_link_libraries(sdl.hdll
        winmm
        opengl32
    )
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
