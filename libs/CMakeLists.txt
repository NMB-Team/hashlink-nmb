set(INCLUDES_BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if (BUILD_SHARED_LIBS)
    set(HDLL_SUFFIX .hdll)
else()
    set(HDLL_SUFFIX .hdll${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

function(set_as_hdll target)
    set_target_properties(${target}.hdll
        PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${target}
        SUFFIX ${HDLL_SUFFIX}
    )
    if (NOT MSVC AND NOT APPLE)
        target_link_options(${target}.hdll PRIVATE "-Wl,--no-undefined")
    endif()
endfunction()

if (DOWNLOAD_DEPENDENCIES)
    set(CMAKE_COMMON_DEP_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_MESSAGE_LOG_LEVEL=ERROR
        -DCMAKE_INSTALL_MESSAGE=LAZY
    )
    if (ANDROID)
        list(APPEND CMAKE_COMMON_DEP_ARGS
            -DANDROID_PLATFORM=${ANDROID_PLATFORM}
            -DANDROID_ABI=${CMAKE_ANDROID_ARCH_ABI}
        )
    endif()
    if (APPLE)
        list(APPEND CMAKE_COMMON_DEP_ARGS
            -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
            -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        )
    endif()
    include(ExternalProject)
    include(FetchContent)
endif()

option(WITH_FMT "Build fmt.hdll." ON)
if(WITH_FMT)
    add_subdirectory(fmt)
endif()

option(WITH_OPENAL "Build openal.hdll." ON)
if(WITH_OPENAL)
    add_subdirectory(openal)
endif()

set(LIMEN_PREBUILT_DIR "" CACHE PATH "Directory containing prebuilt LIMEN modules and runtime libraries.")
option(WITH_LIMEN "Build the LIMEN platform and graphics modules from source." ON)
if(WITH_LIMEN AND LIMEN_PREBUILT_DIR)
    message(FATAL_ERROR "WITH_LIMEN and LIMEN_PREBUILT_DIR cannot be used together.")
endif()
if(WITH_LIMEN)
    set(LIMEN_SOURCE_DIR "" CACHE PATH "Path to an external LIMEN checkout.")
    if(NOT LIMEN_SOURCE_DIR)
        set(LIMEN_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/limen")
        if(NOT EXISTS "${LIMEN_SOURCE_DIR}/CMakeLists.txt")
            find_package(Git REQUIRED)
            message(STATUS "Initializing the LIMEN submodule")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}"
                    submodule update --init --recursive -- libs/limen
                RESULT_VARIABLE LIMEN_SUBMODULE_RESULT
            )
            if(NOT LIMEN_SUBMODULE_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to initialize the LIMEN submodule.")
            endif()
        endif()
    endif()
    if(NOT EXISTS "${LIMEN_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "LIMEN_SOURCE_DIR is not a LIMEN checkout: ${LIMEN_SOURCE_DIR}")
    endif()
    get_filename_component(LIMEN_SOURCE_DIR "${LIMEN_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    add_subdirectory("${LIMEN_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/limen")
elseif(LIMEN_PREBUILT_DIR)
    get_filename_component(LIMEN_PREBUILT_DIR "${LIMEN_PREBUILT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    file(GLOB LIMEN_PREBUILT_FILES "${LIMEN_PREBUILT_DIR}/*")
    if(NOT LIMEN_PREBUILT_FILES)
        message(FATAL_ERROR "LIMEN_PREBUILT_DIR is empty: ${LIMEN_PREBUILT_DIR}")
    endif()
    install(FILES ${LIMEN_PREBUILT_FILES} DESTINATION ${HDLL_DESTINATION})
endif()

option(WITH_SQLITE "Build sqlite.hdll." ON)
if(WITH_SQLITE)
    add_subdirectory(sqlite)
endif()

option(WITH_SSL "Build ssl.hdll." ON)
if(WITH_SSL)
    add_subdirectory(ssl)
endif()

option(WITH_UI "Build ui.hdll." ON)
if(WITH_UI)
    add_subdirectory(ui)
endif()

option(WITH_UV "Build uv.hdll." ON)
if(WITH_UV)
    add_subdirectory(uv)
endif()

option(WITH_VIDEO "Build video.hdll." ON)
if(WIN32)
    if(WITH_VIDEO)
        add_subdirectory(video)
    endif()
endif()

option(WITH_HEAPS "Build heaps.hdll." ON)
if(WITH_HEAPS)
    add_subdirectory(heaps)
endif()
