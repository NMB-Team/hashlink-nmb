set(MINIMP3_INCLUDE_DIR ${INCLUDES_BASE_DIR}/minimp3)
set(MIKKTSPACE_INCLUDE_DIR ${INCLUDES_BASE_DIR}/mikktspace)
set(SPNG_INCLUDE_DIR ${INCLUDES_BASE_DIR}/spng)

function(add_fmt_zlibng)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(INSTALL_UTILS OFF)
    set(ZLIB_ALIASES OFF)
    set(ZLIB_COMPAT ON)
    set(WITH_ALL_FALLBACKS OFF)
    set(WITH_BENCHMARKS OFF)
    set(WITH_BENCHMARK_APPS OFF)
    set(WITH_FUZZERS OFF)
    set(WITH_GTEST OFF)
    set(WITH_NATIVE_INSTRUCTIONS OFF)
    set(WITH_OPTIM ON)
    set(WITH_RUNTIME_CPU_DETECTION ON)

    set(_zlibng_source_dir "${INCLUDES_BASE_DIR}/zlib-ng")
    if(NOT EXISTS "${_zlibng_source_dir}/CMakeLists.txt")
        include(FetchContent)
        FetchContent_Declare(fmt_zlibng
            URL https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.3.3.tar.gz
            URL_HASH SHA256=f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
            SOURCE_DIR "${_zlibng_source_dir}"
        )
        FetchContent_GetProperties(fmt_zlibng)
        if(POLICY CMP0169)
            cmake_policy(PUSH)
            cmake_policy(SET CMP0169 OLD)
        endif()
        if(NOT fmt_zlibng_POPULATED)
            FetchContent_Populate(fmt_zlibng)
        endif()
        if(POLICY CMP0169)
            cmake_policy(POP)
        endif()
    endif()

    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
    add_subdirectory("${_zlibng_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/zlib-ng" EXCLUDE_FROM_ALL)
endfunction()

add_fmt_zlibng()

add_library(fmt.hdll
    fmt.c
    sha1.c
    dxt.c
    mikkt.c
    ${MIKKTSPACE_INCLUDE_DIR}/mikktspace.c
)

target_sources(fmt.hdll PRIVATE
    ${SPNG_INCLUDE_DIR}/spng.c)
target_compile_definitions(fmt.hdll PRIVATE SPNG_STATIC)

include(CheckCSourceCompiles)
check_c_source_compiles("\
#if !defined(__x86_64__) && !defined(_M_X64)\n\
#error target clones are only used by libspng on x86-64\n\
#endif\n\
__attribute__((target_clones(\"default,avx2\"))) static int spng_clone_test(int value) { return value; }\n\
int main(void) { return spng_clone_test(0); }\n\
" SPNG_HAS_TARGET_CLONES)
if(SPNG_HAS_TARGET_CLONES)
    set_property(SOURCE "${SPNG_INCLUDE_DIR}/spng.c" APPEND PROPERTY COMPILE_DEFINITIONS SPNG_ENABLE_TARGET_CLONES)
endif()

if (WIN32)
    set(VORBIS_INCLUDE_DIR ${INCLUDES_BASE_DIR}/vorbis)

    target_sources(fmt.hdll PRIVATE
        ${INCLUDES_BASE_DIR}/vorbis/bitrate.c
        ${INCLUDES_BASE_DIR}/vorbis/bitwise.c
        ${INCLUDES_BASE_DIR}/vorbis/block.c
        ${INCLUDES_BASE_DIR}/vorbis/codebook.c
        ${INCLUDES_BASE_DIR}/vorbis/envelope.c
        ${INCLUDES_BASE_DIR}/vorbis/floor0.c
        ${INCLUDES_BASE_DIR}/vorbis/floor1.c
        ${INCLUDES_BASE_DIR}/vorbis/framing.c
        ${INCLUDES_BASE_DIR}/vorbis/info.c
        ${INCLUDES_BASE_DIR}/vorbis/lookup.c
        ${INCLUDES_BASE_DIR}/vorbis/lpc.c
        ${INCLUDES_BASE_DIR}/vorbis/lsp.c
        ${INCLUDES_BASE_DIR}/vorbis/mapping0.c
        ${INCLUDES_BASE_DIR}/vorbis/mdct.c
        ${INCLUDES_BASE_DIR}/vorbis/psy.c
        ${INCLUDES_BASE_DIR}/vorbis/registry.c
        ${INCLUDES_BASE_DIR}/vorbis/res0.c
        ${INCLUDES_BASE_DIR}/vorbis/sharedbook.c
        ${INCLUDES_BASE_DIR}/vorbis/smallft.c
        ${INCLUDES_BASE_DIR}/vorbis/synthesis.c
        ${INCLUDES_BASE_DIR}/vorbis/vorbisfile.c
        ${INCLUDES_BASE_DIR}/vorbis/window.c
    )
endif()

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TurboJPEG_INCLUDE_DIRS ${INCLUDES_BASE_DIR}/turbojpeg ${INCLUDES_BASE_DIR}/turbojpeg/x64)
        find_library(TurboJPEG_LIBRARIES simd PATHS ${INCLUDES_BASE_DIR}/turbojpeg/x64)
    else()
        set(TurboJPEG_INCLUDE_DIRS ${INCLUDES_BASE_DIR}/turbojpeg ${INCLUDES_BASE_DIR}/turbojpeg/x86)
        find_library(TurboJPEG_LIBRARIES simd PATHS ${INCLUDES_BASE_DIR}/turbojpeg/x86)
    endif()

    target_sources(fmt.hdll PRIVATE
        ${INCLUDES_BASE_DIR}/turbojpeg/jaricom.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcapimin.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcapistd.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcarith.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jccoefct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jccolor.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcdctmgr.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jchuff.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcinit.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcmainct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcmarker.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcmaster.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcomapi.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcparam.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcphuff.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcprepct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jcsample.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jctrans.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdapimin.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdapistd.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdarith.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdatadst-tj.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdatadst.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdatasrc-tj.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdatasrc.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdcoefct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdcolor.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jddctmgr.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdhuff.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdinput.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdmainct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdmarker.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdmaster.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdmerge.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdphuff.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdpostct.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdsample.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jdtrans.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jerror.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jfdctflt.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jfdctfst.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jfdctint.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jidctflt.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jidctfst.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jidctint.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jidctred.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jmemmgr.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jmemnobs.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jquant1.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jquant2.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jsimd.c
        ${INCLUDES_BASE_DIR}/turbojpeg/jutils.c
        ${INCLUDES_BASE_DIR}/turbojpeg/transupp.c
        ${INCLUDES_BASE_DIR}/turbojpeg/turbojpeg.c
    )
elseif(DOWNLOAD_DEPENDENCIES)
	ExternalProject_Add(turbojpeg-project
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.1/libjpeg-turbo-3.1.1.tar.gz
        URL_HASH SHA256=aadc97ea91f6ef078b0ae3a62bba69e008d9a7db19b34e4ac973b19b71b4217c
        CMAKE_ARGS
            ${CMAKE_COMMON_DEP_ARGS}
            -DBUILD_SHARED_LIBS=OFF
            -DCMAKE_INSTALL_LIBDIR=lib
        # INSTALL_BYPRODUCTS in CMake 3.26+
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libturbojpeg.a
    )

    ExternalProject_Get_Property(turbojpeg-project INSTALL_DIR)

    add_library(turbojpeg STATIC IMPORTED)
    set_target_properties(turbojpeg PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libturbojpeg.a)

    set(TurboJPEG_INCLUDE_DIRS ${INSTALL_DIR}/${CMAKE_INSTALL_INCLUDEDIR})
    set(TurboJPEG_LIBRARIES turbojpeg)

    add_dependencies(turbojpeg turbojpeg-project)

    ExternalProject_Add(libogg
        URL https://github.com/xiph/ogg/releases/download/v1.3.6/libogg-1.3.6.tar.gz
        URL_HASH SHA256=83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638
        CMAKE_ARGS
            ${CMAKE_COMMON_DEP_ARGS}
            -DCMAKE_INSTALL_LIBDIR=lib
        # INSTALL_BYPRODUCTS in CMake 3.26+
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libogg.a
    )
    ExternalProject_Get_Property(libogg INSTALL_DIR)
    set(OGG_INSTALL_DIR ${INSTALL_DIR})

    set(OGG_LIBRARY ${OGG_INSTALL_DIR}/lib/libogg.a)
    set(OGG_INCLUDE_DIR ${OGG_INSTALL_DIR}/${CMAKE_INSTALL_INCLUDEDIR})

    ExternalProject_Add(libvorbis
        URL https://github.com/xiph/vorbis/releases/download/v1.3.7/libvorbis-1.3.7.tar.xz
        URL_HASH SHA256=b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b
        CMAKE_ARGS
            ${CMAKE_COMMON_DEP_ARGS}
            -DOGG_LIBRARY=${OGG_LIBRARY}
            -DOGG_INCLUDE_DIR=${OGG_INCLUDE_DIR}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5 # https://github.com/xiph/vorbis/issues/113
        # INSTALL_BYPRODUCTS in CMake 3.26+
        BUILD_BYPRODUCTS
            <INSTALL_DIR>/lib/libvorbis.a
            <INSTALL_DIR>/lib/libvorbisfile.a
    )
    add_dependencies(libvorbis libogg)
    ExternalProject_Get_Property(libvorbis INSTALL_DIR)
    set(VORBIS_INSTALL_DIR ${INSTALL_DIR})

    add_library(ogg STATIC IMPORTED)
    set_target_properties(ogg PROPERTIES IMPORTED_LOCATION ${OGG_LIBRARY})

    add_library(vorbis STATIC IMPORTED)
    set_target_properties(vorbis PROPERTIES IMPORTED_LOCATION ${VORBIS_INSTALL_DIR}/lib/libvorbis.a)
    add_library(vorbisfile STATIC IMPORTED)
    set_target_properties(vorbisfile PROPERTIES IMPORTED_LOCATION ${VORBIS_INSTALL_DIR}/lib/libvorbisfile.a)

    set(VORBIS_INCLUDE_DIR ${VORBIS_INSTALL_DIR}/${CMAKE_INSTALL_INCLUDEDIR} ${OGG_INCLUDE_DIR})
    set(OGGVORBIS_LIBRARIES vorbisfile vorbis ogg)

    add_dependencies(vorbisfile libvorbis)
    add_dependencies(vorbis libvorbis)
    add_dependencies(ogg libogg)
else()
    find_package(TurboJPEG QUIET)
    if(NOT TurboJPEG_FOUND)
        pkg_check_modules(TurboJPEG REQUIRED libjpeg)
    endif()

    find_package(OggVorbis QUIET)
    if(NOT OGGVORBIS_FOUND)
        pkg_check_modules(OGGVORBIS REQUIRED vorbis vorbisenc vorbisfile)
    endif()
endif()

set_as_hdll(fmt)

target_include_directories(fmt.hdll
    PRIVATE
    ${SPNG_INCLUDE_DIR}
    ${TurboJPEG_INCLUDE_DIRS}
    ${VORBIS_INCLUDE_DIR}
    ${MINIMP3_INCLUDE_DIR}
    ${MIKKTSPACE_INCLUDE_DIR}
)

target_link_libraries(fmt.hdll
    libhl
    zlib-ng::zlibstatic
    ${TurboJPEG_LIBRARIES}
    ${OGGVORBIS_LIBRARIES}
)

install(
    TARGETS
        fmt.hdll
    DESTINATION ${HDLL_DESTINATION}
)
