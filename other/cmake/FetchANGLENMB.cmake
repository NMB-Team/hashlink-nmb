include_guard(GLOBAL)

function(_angle_nmb_fail message_text)
    message(FATAL_ERROR "${message_text}")
endfunction()

function(_angle_nmb_require_https url description)
    if(ANGLE_NMB_TESTING)
        return()
    endif()

    if(NOT "${url}" MATCHES "^https://")
        _angle_nmb_fail("${description} must use HTTPS: ${url}")
    endif()
endfunction()

function(_angle_nmb_json_get output json)
    string(JSON value ERROR_VARIABLE error GET "${json}" ${ARGN})
    if(NOT error STREQUAL "NOTFOUND")
        string(JOIN "." field ${ARGN})
        _angle_nmb_fail("ANGLE manifest is missing or has an invalid '${field}' field: ${error}")
    endif()
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

function(_angle_nmb_download_manifest url destination)
    _angle_nmb_require_https("${url}" "ANGLE manifest URL")

    set(temporary_path "${destination}.tmp")
    file(REMOVE "${temporary_path}")
    file(
        DOWNLOAD
        "${url}"
        "${temporary_path}"
        TLS_VERIFY ON
        STATUS download_status
    )
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    if(NOT status_code EQUAL 0)
        file(REMOVE "${temporary_path}")
        _angle_nmb_fail("ANGLE manifest download failed: ${status_message}")
    endif()

    file(RENAME "${temporary_path}" "${destination}")
endfunction()

function(_angle_nmb_download_archive url destination sha256)
    _angle_nmb_require_https("${url}" "ANGLE archive URL")

    set(temporary_path "${destination}.tmp")
    file(REMOVE "${temporary_path}")
    file(
        DOWNLOAD
        "${url}"
        "${temporary_path}"
        EXPECTED_HASH "SHA256=${sha256}"
        SHOW_PROGRESS
        TLS_VERIFY ON
        STATUS download_status
    )
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_message)
    if(NOT status_code EQUAL 0)
        file(REMOVE "${temporary_path}")
        _angle_nmb_fail("ANGLE archive download or SHA-256 verification failed: ${status_message}")
    endif()

    file(RENAME "${temporary_path}" "${destination}")
endfunction()

function(_angle_nmb_validate_package package_root expected_revision output_revision output_renderer)
    set(required_files
        ANGLE_REVISION
        VERSION
        BUILD_INFO.json
        LICENSE
        include/EGL/egl.h
        include/EGL/eglext.h
        include/GLES3/gl3.h
    )

    if(WIN32)
        list(APPEND required_files
            bin/x64/libEGL.dll
            bin/x64/libGLESv2.dll
            lib/x64/libEGL.lib
            lib/x64/libGLESv2.lib
        )
        set(expected_platform "windows")
        set(expected_renderer "Vulkan")
    elseif(APPLE)
        list(APPEND required_files
            lib/universal/libEGL.dylib
            lib/universal/libGLESv2.dylib
        )
        set(expected_platform "macos")
        set(expected_renderer "Metal")
    else()
        list(APPEND required_files
            lib/x64/libEGL.so
            lib/x64/libGLESv2.so
        )
        set(expected_platform "linux")
        set(expected_renderer "Vulkan")
    endif()

    foreach(required_file IN LISTS required_files)
        if(NOT EXISTS "${package_root}/${required_file}")
            _angle_nmb_fail("ANGLE package is missing '${required_file}': ${package_root}")
        endif()
    endforeach()

    file(READ "${package_root}/ANGLE_REVISION" package_revision)
    string(STRIP "${package_revision}" package_revision)
    if(package_revision STREQUAL "")
        _angle_nmb_fail("ANGLE package contains an empty ANGLE_REVISION file: ${package_root}")
    endif()
    if(NOT "${expected_revision}" STREQUAL "" AND NOT package_revision STREQUAL expected_revision)
        _angle_nmb_fail(
            "ANGLE package revision '${package_revision}' does not match manifest revision '${expected_revision}'."
        )
    endif()

    file(READ "${package_root}/BUILD_INFO.json" build_info)
    _angle_nmb_json_get(build_platform "${build_info}" platform)
    _angle_nmb_json_get(build_revision "${build_info}" angleRevision)
    if(NOT build_platform STREQUAL expected_platform)
        _angle_nmb_fail(
            "ANGLE package platform '${build_platform}' does not match expected platform '${expected_platform}'."
        )
    endif()
    if(NOT build_revision STREQUAL package_revision)
        _angle_nmb_fail(
            "ANGLE BUILD_INFO.json revision '${build_revision}' does not match ANGLE_REVISION '${package_revision}'."
        )
    endif()

    string(JSON targets_length ERROR_VARIABLE targets_error LENGTH "${build_info}" targets)
    if(NOT targets_error STREQUAL "NOTFOUND" OR targets_length LESS 1)
        _angle_nmb_fail("ANGLE BUILD_INFO.json must contain at least one build target.")
    endif()

    set(build_renderer "")
    math(EXPR targets_last "${targets_length} - 1")
    foreach(target_index RANGE 0 ${targets_last})
        _angle_nmb_json_get(target_renderer "${build_info}" targets ${target_index} renderer)
        if(NOT target_renderer STREQUAL expected_renderer)
            _angle_nmb_fail(
                "ANGLE package target renderer '${target_renderer}' does not match expected renderer '${expected_renderer}'."
            )
        endif()
        set(build_renderer "${target_renderer}")
    endforeach()

    string(JSON metadata_renderer ERROR_VARIABLE renderer_error GET "${build_info}" renderer)
    if(renderer_error STREQUAL "NOTFOUND")
        if(NOT metadata_renderer STREQUAL expected_renderer)
            _angle_nmb_fail(
                "ANGLE package renderer '${metadata_renderer}' does not match expected renderer '${expected_renderer}'."
            )
        endif()
        set(build_renderer "${metadata_renderer}")
    endif()

    set(${output_revision} "${package_revision}" PARENT_SCOPE)
    set(${output_renderer} "${build_renderer}" PARENT_SCOPE)
endfunction()

function(_angle_nmb_create_targets package_root)
    if(TARGET ANGLE_NMB::Headers)
        return()
    endif()

    add_library(ANGLE_NMB::Headers INTERFACE IMPORTED GLOBAL)
    set_target_properties(
        ANGLE_NMB::Headers
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${package_root}/include"
    )

    add_library(ANGLE_NMB::EGL SHARED IMPORTED GLOBAL)
    add_library(ANGLE_NMB::GLESv2 SHARED IMPORTED GLOBAL)

    if(WIN32)
        set(egl_runtime "${package_root}/bin/x64/libEGL.dll")
        set(gles_runtime "${package_root}/bin/x64/libGLESv2.dll")
        set_target_properties(
            ANGLE_NMB::EGL
            PROPERTIES
                IMPORTED_IMPLIB "${package_root}/lib/x64/libEGL.lib"
                IMPORTED_LOCATION "${egl_runtime}"
        )
        set_target_properties(
            ANGLE_NMB::GLESv2
            PROPERTIES
                IMPORTED_IMPLIB "${package_root}/lib/x64/libGLESv2.lib"
                IMPORTED_LOCATION "${gles_runtime}"
        )
    elseif(APPLE)
        set(egl_runtime "${package_root}/lib/universal/libEGL.dylib")
        set(gles_runtime "${package_root}/lib/universal/libGLESv2.dylib")
        set_target_properties(
            ANGLE_NMB::EGL
            PROPERTIES IMPORTED_LOCATION "${egl_runtime}"
        )
        set_target_properties(
            ANGLE_NMB::GLESv2
            PROPERTIES IMPORTED_LOCATION "${gles_runtime}"
        )
    else()
        set(egl_runtime "${package_root}/lib/x64/libEGL.so")
        set(gles_runtime "${package_root}/lib/x64/libGLESv2.so")
        set_target_properties(
            ANGLE_NMB::EGL
            PROPERTIES IMPORTED_LOCATION "${egl_runtime}"
        )
        set_target_properties(
            ANGLE_NMB::GLESv2
            PROPERTIES IMPORTED_LOCATION "${gles_runtime}"
        )
    endif()

    set(ANGLE_NMB_EGL_RUNTIME "${egl_runtime}" CACHE INTERNAL "ANGLE EGL runtime library" FORCE)
    set(ANGLE_NMB_GLES_RUNTIME "${gles_runtime}" CACHE INTERNAL "ANGLE GLES runtime library" FORCE)
endfunction()

function(_angle_nmb_export_result source package_root builder_commit builder_tag angle_revision renderer)
    set(ANGLE_NMB_SOURCE "${source}" CACHE INTERNAL "Resolved ANGLE package source" FORCE)
    set(ANGLE_NMB_PACKAGE_ROOT "${package_root}" CACHE INTERNAL "Resolved ANGLE package root" FORCE)
    set(ANGLE_NMB_RESOLVED_BUILDER_COMMIT "${builder_commit}" CACHE INTERNAL "Resolved ANGLE builder commit" FORCE)
    set(ANGLE_NMB_RESOLVED_BUILDER_TAG "${builder_tag}" CACHE INTERNAL "Resolved ANGLE builder tag" FORCE)
    set(ANGLE_NMB_RESOLVED_ANGLE_REVISION "${angle_revision}" CACHE INTERNAL "Resolved ANGLE revision" FORCE)
    set(ANGLE_NMB_RESOLVED_RENDERER "${renderer}" CACHE INTERNAL "Resolved ANGLE renderer" FORCE)

    foreach(variable
        ANGLE_NMB_SOURCE
        ANGLE_NMB_PACKAGE_ROOT
        ANGLE_NMB_RESOLVED_BUILDER_COMMIT
        ANGLE_NMB_RESOLVED_BUILDER_TAG
        ANGLE_NMB_RESOLVED_ANGLE_REVISION
        ANGLE_NMB_RESOLVED_RENDERER
        ANGLE_NMB_PLATFORM_KEY
    )
        set(${variable} "${${variable}}" PARENT_SCOPE)
    endforeach()

    message(STATUS "ANGLE NMB:")
    message(STATUS "  source: ${source}")
    message(STATUS "  platform: ${ANGLE_NMB_PLATFORM_KEY}")
    message(STATUS "  builder commit: ${builder_commit}")
    message(STATUS "  ANGLE revision: ${angle_revision}")
    message(STATUS "  renderer: ${renderer}")
    message(STATUS "  package root: ${package_root}")
endfunction()

function(angle_nmb_resolve_package)
    if(CMAKE_VERSION VERSION_LESS 3.19)
        _angle_nmb_fail("WITH_SDL_ANGLE requires CMake 3.19 or newer.")
    endif()

    if(ANDROID)
        _angle_nmb_fail("Desktop ANGLE NMB packages cannot be used on Android.")
    elseif(WIN32)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            _angle_nmb_fail("ANGLE NMB currently supports Windows x64 only.")
        endif()
        set(ANGLE_NMB_PLATFORM_KEY "windows-x64")
    elseif(APPLE)
        set(ANGLE_NMB_PLATFORM_KEY "macos-universal")
    elseif(UNIX)
        if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
            _angle_nmb_fail("ANGLE NMB currently supports Linux x64 only.")
        endif()
        set(ANGLE_NMB_PLATFORM_KEY "linux-x64")
    else()
        _angle_nmb_fail("ANGLE NMB does not support this platform.")
    endif()
    set(ANGLE_NMB_PLATFORM_KEY "${ANGLE_NMB_PLATFORM_KEY}" CACHE INTERNAL "ANGLE package platform key" FORCE)

    if(NOT "${ANGLE_NMB_ROOT}" STREQUAL "")
        get_filename_component(local_package_root "${ANGLE_NMB_ROOT}" ABSOLUTE)
        _angle_nmb_validate_package("${local_package_root}" "" package_revision renderer)
        _angle_nmb_create_targets("${local_package_root}")
        message(STATUS "ANGLE NMB local package override is active.")
        _angle_nmb_export_result(
            "local"
            "${local_package_root}"
            "local"
            "local"
            "${package_revision}"
            "${renderer}"
        )
        return()
    endif()

    if(NOT ANGLE_NMB_AUTO_DOWNLOAD)
        _angle_nmb_fail(
            "WITH_SDL_ANGLE is enabled, but ANGLE_NMB_AUTO_DOWNLOAD is OFF and ANGLE_NMB_ROOT is empty."
        )
    endif()

    set(download_root "${CMAKE_BINARY_DIR}/_deps/angle-nmb")
    file(MAKE_DIRECTORY "${download_root}")

    set(requested_short_commit "")
    if(NOT "${ANGLE_NMB_BUILDER_COMMIT}" STREQUAL "")
        string(LENGTH "${ANGLE_NMB_BUILDER_COMMIT}" requested_commit_length)
        if(requested_commit_length LESS 12)
            _angle_nmb_fail("ANGLE_NMB_BUILDER_COMMIT must contain at least 12 commit characters.")
        endif()
        string(SUBSTRING "${ANGLE_NMB_BUILDER_COMMIT}" 0 12 requested_short_commit)
        string(
            REGEX REPLACE
            "/releases/download/[^/]+/[^/]+$"
            "/releases/download/build-${requested_short_commit}/manifest.json"
            manifest_url
            "${ANGLE_NMB_MANIFEST_URL}"
        )
        if(manifest_url STREQUAL ANGLE_NMB_MANIFEST_URL)
            _angle_nmb_fail(
                "ANGLE_NMB_BUILDER_COMMIT requires a release manifest URL ending in '/releases/download/<tag>/<file>'."
            )
        endif()
        set(manifest_path "${download_root}/${requested_short_commit}/manifest.json")
    else()
        set(manifest_url "${ANGLE_NMB_MANIFEST_URL}")
        set(manifest_path "${download_root}/latest.json")
    endif()

    get_filename_component(manifest_directory "${manifest_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${manifest_directory}")

    if(ANGLE_NMB_OFFLINE)
        if(NOT EXISTS "${manifest_path}")
            _angle_nmb_fail(
                "ANGLE_NMB_OFFLINE is ON, ANGLE_NMB_ROOT is empty, and no cached ANGLE manifest exists at '${manifest_path}'."
            )
        endif()
        message(STATUS "ANGLE NMB offline mode: using cached manifest ${manifest_path}")
    else()
        _angle_nmb_download_manifest("${manifest_url}" "${manifest_path}")
        message(STATUS "ANGLE manifest downloaded: ${manifest_url}")
    endif()

    file(READ "${manifest_path}" manifest)
    _angle_nmb_json_get(schema_version "${manifest}" schemaVersion)
    if(NOT schema_version EQUAL 1)
        _angle_nmb_fail("Unsupported ANGLE manifest schema version '${schema_version}'.")
    endif()

    _angle_nmb_json_get(builder_commit "${manifest}" builderCommit)
    _angle_nmb_json_get(builder_tag "${manifest}" builderTag)
    _angle_nmb_json_get(angle_revision "${manifest}" angleRevision)
    _angle_nmb_json_get(artifact_name "${manifest}" artifacts "${ANGLE_NMB_PLATFORM_KEY}" name)
    _angle_nmb_json_get(artifact_url "${manifest}" artifacts "${ANGLE_NMB_PLATFORM_KEY}" url)
    _angle_nmb_json_get(artifact_sha256 "${manifest}" artifacts "${ANGLE_NMB_PLATFORM_KEY}" sha256)

    set(expected_artifact_name "angle-nmb-${ANGLE_NMB_PLATFORM_KEY}.zip")
    if(NOT artifact_name STREQUAL expected_artifact_name)
        _angle_nmb_fail(
            "ANGLE manifest artifact name '${artifact_name}' does not match expected package '${expected_artifact_name}'."
        )
    endif()

    if(NOT requested_short_commit STREQUAL "")
        string(FIND "${builder_commit}" "${ANGLE_NMB_BUILDER_COMMIT}" requested_commit_position)
        if(NOT requested_commit_position EQUAL 0)
            _angle_nmb_fail(
                "ANGLE immutable manifest resolved builder commit '${builder_commit}', not requested commit '${ANGLE_NMB_BUILDER_COMMIT}'."
            )
        endif()
    endif()

    string(LENGTH "${builder_commit}" builder_commit_length)
    if(builder_commit_length LESS 12)
        _angle_nmb_fail("ANGLE manifest builderCommit must contain at least 12 commit characters.")
    endif()
    string(SUBSTRING "${builder_commit}" 0 12 short_commit)
    if(NOT builder_tag STREQUAL "build-${short_commit}")
        _angle_nmb_fail(
            "ANGLE manifest builderTag '${builder_tag}' does not match builder commit '${builder_commit}'."
        )
    endif()
    string(LENGTH "${artifact_sha256}" artifact_sha256_length)
    if(NOT artifact_sha256_length EQUAL 64 OR NOT artifact_sha256 MATCHES "^[0-9A-Fa-f]+$")
        _angle_nmb_fail("ANGLE manifest contains an invalid SHA-256 for '${ANGLE_NMB_PLATFORM_KEY}'.")
    endif()
    _angle_nmb_require_https("${artifact_url}" "ANGLE artifact URL")
    if(NOT artifact_url MATCHES "/releases/download/${builder_tag}/${artifact_name}$")
        _angle_nmb_fail(
            "ANGLE artifact URL must reference immutable release '${builder_tag}': ${artifact_url}"
        )
    endif()

    set(cache_root "${download_root}/${short_commit}/${ANGLE_NMB_PLATFORM_KEY}")
    set(package_root "${cache_root}/package")
    set(package_marker "${package_root}/.angle-nmb-ready")
    set(archive_path "${cache_root}/${artifact_name}")
    file(MAKE_DIRECTORY "${cache_root}")

    message(STATUS "ANGLE resolved builder commit: ${builder_commit}")
    message(STATUS "ANGLE resolved ANGLE revision: ${angle_revision}")
    message(STATUS "ANGLE selected package: ${artifact_name}")
    message(STATUS "ANGLE package cache path: ${cache_root}")

    if(EXISTS "${package_root}" AND NOT EXISTS "${package_marker}")
        message(STATUS "Removing incomplete ANGLE package cache: ${package_root}")
        file(REMOVE_RECURSE "${package_root}")
    endif()

    if(EXISTS "${archive_path}")
        file(SHA256 "${archive_path}" cached_sha256)
        if(NOT cached_sha256 STREQUAL artifact_sha256)
            message(STATUS "Removing ANGLE cache with an invalid archive SHA-256.")
            file(REMOVE "${archive_path}")
            file(REMOVE_RECURSE "${package_root}")
        else()
            message(STATUS "ANGLE archive SHA-256 verified from cache.")
        endif()
    elseif(EXISTS "${package_marker}")
        message(STATUS "Removing ANGLE package cache without its verified archive.")
        file(REMOVE_RECURSE "${package_root}")
    endif()

    if(EXISTS "${package_marker}")
        _angle_nmb_validate_package("${package_root}" "${angle_revision}" package_revision renderer)
        message(STATUS "Reusing validated cached ANGLE package: ${package_root}")
    else()
        if(NOT EXISTS "${archive_path}")
            if(ANGLE_NMB_OFFLINE)
                _angle_nmb_fail(
                    "ANGLE_NMB_OFFLINE is ON and the verified archive is not cached at '${archive_path}'."
                )
            endif()
            _angle_nmb_download_archive("${artifact_url}" "${archive_path}" "${artifact_sha256}")
            message(STATUS "ANGLE archive SHA-256 verified: ${artifact_sha256}")
        endif()

        set(temporary_package_root "${cache_root}/package.tmp")
        file(REMOVE_RECURSE "${temporary_package_root}")
        file(MAKE_DIRECTORY "${temporary_package_root}")
        file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${temporary_package_root}")
        _angle_nmb_validate_package(
            "${temporary_package_root}"
            "${angle_revision}"
            package_revision
            renderer
        )
        file(WRITE "${temporary_package_root}/.angle-nmb-ready" "${builder_commit}\n")
        file(RENAME "${temporary_package_root}" "${package_root}")
    endif()

    configure_file("${manifest_path}" "${cache_root}/manifest.json" COPYONLY)
    _angle_nmb_create_targets("${package_root}")
    _angle_nmb_export_result(
        "downloaded"
        "${package_root}"
        "${builder_commit}"
        "${builder_tag}"
        "${package_revision}"
        "${renderer}"
    )
endfunction()
