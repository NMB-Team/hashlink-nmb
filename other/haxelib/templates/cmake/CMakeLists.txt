$$setDefaultJumboBuild(true)cmake_minimum_required(VERSION 3.20)

project(hlc_generated LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(HASHLINK "" CACHE PATH "HashLink installation root")
set(HLC_NATIVE_PATH "" CACHE STRING "Additional native-module directories (CMake list)")
set(HLC_ARCH "native" CACHE STRING "Target architecture: native, x86_64, arm64")
option(HLC_COPY_RUNTIME_MODULES "Copy HashLink/LIMEN runtime modules next to the executable" ON)

if(NOT HASHLINK AND DEFINED ENV{HASHLINK} AND NOT "$ENV{HASHLINK}" STREQUAL "")
	file(TO_CMAKE_PATH "$ENV{HASHLINK}" HASHLINK)
endif()

set(_hl_include_hints)
if(HASHLINK)
	list(APPEND _hl_include_hints
		"${HASHLINK}/include"
		"${HASHLINK}/src"
		"${HASHLINK}"
	)
endif()

find_path(HLC_HASHLINK_INCLUDE_DIR
	NAMES hlc.h
	HINTS ${_hl_include_hints}
	PATH_SUFFIXES include src
)

if(NOT HLC_HASHLINK_INCLUDE_DIR)
	message(FATAL_ERROR
		"Could not find hlc.h. Set HASHLINK or pass -DHASHLINK=/path/to/hashlink."
	)
endif()

if(NOT HASHLINK)
	get_filename_component(_hl_include_leaf "${HLC_HASHLINK_INCLUDE_DIR}" NAME)
	if(_hl_include_leaf STREQUAL "include" OR _hl_include_leaf STREQUAL "src")
		get_filename_component(HASHLINK "${HLC_HASHLINK_INCLUDE_DIR}" DIRECTORY)
	else()
		set(HASHLINK "${HLC_HASHLINK_INCLUDE_DIR}")
	endif()
	set(HASHLINK "${HASHLINK}" CACHE PATH "HashLink installation root" FORCE)
endif()

set(HLC_NATIVE_SEARCH_DIRS
	${HLC_NATIVE_PATH}
	"${HASHLINK}"
	"${HASHLINK}/lib"
	"${HASHLINK}/bin"
)

if(WIN32)
	list(APPEND HLC_NATIVE_SEARCH_DIRS
		"${HASHLINK}/x64/Debug"
		"${HASHLINK}/x64/Release"
		"${HASHLINK}/ARM64/Debug"
		"${HASHLINK}/ARM64/Release"
	)
endif()

list(REMOVE_DUPLICATES HLC_NATIVE_SEARCH_DIRS)

function(hlc_find_native_file out_var module_name)
	string(REGEX REPLACE "^\\?" "" _module "${module_name}")
	set(_names "${_module}")
	if(NOT _module MATCHES "\\.(hdll|limen)$")
		list(APPEND _names "${_module}.hdll")
	endif()

	foreach(_dir IN LISTS HLC_NATIVE_SEARCH_DIRS)
		if(NOT IS_DIRECTORY "${_dir}")
			continue()
		endif()
		foreach(_name IN LISTS _names)
			if(EXISTS "${_dir}/${_name}")
				set(${out_var} "${_dir}/${_name}" PARENT_SCOPE)
				return()
			endif()
		endforeach()
	endforeach()

	set(${out_var} "" PARENT_SCOPE)
endfunction()

function(hlc_find_windows_import_library out_var module_name)
	string(REGEX REPLACE "^\\?" "" _module "${module_name}")
	set(_base "${_module}")
	set(_names)

	if(_base MATCHES "\\.hdll$")
		string(REGEX REPLACE "\\.hdll$" "" _base "${_base}")
	endif()

	if(_module MATCHES "\\.limen$")
		list(APPEND _names "${_module}.lib" "limen.lib")
	else()
		list(APPEND _names "${_module}.lib" "${_base}.lib")
	endif()

	list(REMOVE_DUPLICATES _names)

	foreach(_dir IN LISTS HLC_NATIVE_SEARCH_DIRS)
		if(NOT IS_DIRECTORY "${_dir}")
			continue()
		endif()
		foreach(_name IN LISTS _names)
			if(EXISTS "${_dir}/${_name}")
				set(${out_var} "${_dir}/${_name}" PARENT_SCOPE)
				return()
			endif()
		endforeach()
	endforeach()

	set(${out_var} "" PARENT_SCOPE)
endfunction()

if(HLC_ARCH STREQUAL "x86_64")
	if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
		message(FATAL_ERROR "HLC_ARCH=x86_64 requires a 64-bit compiler/toolchain.")
	endif()
elseif(HLC_ARCH STREQUAL "arm64")
	if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
		message(FATAL_ERROR "HLC_ARCH=arm64 requires a 64-bit compiler/toolchain.")
	endif()
elseif(NOT HLC_ARCH STREQUAL "native")
	message(FATAL_ERROR "Unsupported HLC_ARCH='${HLC_ARCH}'.")
endif()

set(HLC_GENERATED_DIR "::relDir::")
if(HLC_GENERATED_DIR STREQUAL "")
	set(HLC_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
elseif(NOT IS_ABSOLUTE "${HLC_GENERATED_DIR}")
	get_filename_component(
		HLC_GENERATED_DIR
		"${CMAKE_CURRENT_SOURCE_DIR}/${HLC_GENERATED_DIR}"
		ABSOLUTE
	)
endif()

::if (jumboBuild == "true")::
set(HLC_SOURCES
	"${HLC_GENERATED_DIR}/::name::.c"
)
::else::
set(HLC_SOURCES
::foreach cfiles::
	"${HLC_GENERATED_DIR}/::path::"
::end::
)
::end::

add_executable(hlc_app ${HLC_SOURCES})

set_target_properties(hlc_app PROPERTIES
	OUTPUT_NAME "::name::"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(WIN32)
	if(HLC_ARCH STREQUAL "arm64")
		set(_hlc_output_platform "ARM64")
	else()
		set(_hlc_output_platform "x64")
	endif()
	set(_hlc_runtime_output_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/${_hlc_output_platform}"
	)
else()
	set(_hlc_runtime_output_directory "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${_cfg}" _cfg_upper)
	if(WIN32)
		set(_hlc_config_output_directory "${_hlc_runtime_output_directory}/${_cfg}")
	else()
		set(_hlc_config_output_directory "${_hlc_runtime_output_directory}")
	endif()
	set_target_properties(hlc_app PROPERTIES
		"RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper}"
		"${_hlc_config_output_directory}"
	)
endforeach()

if(MSVC)
	set_target_properties(hlc_app PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(hlc_app PRIVATE
	"${HLC_GENERATED_DIR}"
	"${HLC_HASHLINK_INCLUDE_DIR}"
)

::if (jumboBuild != "true")::
target_compile_definitions(hlc_app PRIVATE
	HLC_BOOT
	HL_MAKE
)

target_precompile_headers(hlc_app PRIVATE
	<hlc.h>
)
::end::

if(MSVC)
	target_compile_definitions(hlc_app PRIVATE UNICODE _UNICODE _WINDOWS)
	target_compile_options(hlc_app PRIVATE
		/W3
		/wd4100 /wd4101 /wd4102 /wd4204 /wd4221 /wd4244
		/wd4700 /wd4701 /wd4702 /wd4703 /wd4715 /wd4716 /wd4723
	)
elseif(MINGW)
	target_compile_definitions(hlc_app PRIVATE UNICODE _UNICODE _WINDOWS)
	target_compile_options(hlc_app PRIVATE -municode)
	target_link_options(hlc_app PRIVATE -municode)
endif()

if(WIN32 AND MSVC)
	hlc_find_windows_import_library(HLC_HASHLINK_LIBRARY "libhl")
	if(NOT HLC_HASHLINK_LIBRARY)
		message(FATAL_ERROR "Could not find libhl.lib under HASHLINK='${HASHLINK}'.")
	endif()
else()
	find_library(HLC_HASHLINK_LIBRARY
		NAMES hl
		HINTS ${HLC_NATIVE_SEARCH_DIRS}
		NO_DEFAULT_PATH
	)
	if(NOT HLC_HASHLINK_LIBRARY)
		message(FATAL_ERROR "Could not find libhl under HASHLINK='${HASHLINK}'.")
	endif()
endif()

target_link_libraries(hlc_app PRIVATE "${HLC_HASHLINK_LIBRARY}")

set(HLC_NATIVE_LINK_LIBRARIES)
::foreach libraries::
if(WIN32 AND MSVC)
	hlc_find_windows_import_library(_native_lib "::name::")
else()
	hlc_find_native_file(_native_lib "::name::")
endif()

if(NOT _native_lib)
	message(FATAL_ERROR
		"Could not resolve HashLink native library '::name::'. "
		"Searched: ${HLC_NATIVE_SEARCH_DIRS}"
	)
endif()

list(APPEND HLC_NATIVE_LINK_LIBRARIES "${_native_lib}")
::end::

list(REMOVE_DUPLICATES HLC_NATIVE_LINK_LIBRARIES)
target_link_libraries(hlc_app PRIVATE ${HLC_NATIVE_LINK_LIBRARIES})

if(MSVC)
	target_link_libraries(hlc_app PRIVATE delayimp)
elseif(MINGW)
	target_link_libraries(hlc_app PRIVATE dbghelp)
endif()

if(APPLE)
	set_target_properties(hlc_app PROPERTIES
		BUILD_RPATH "@executable_path;${HASHLINK};${HASHLINK}/lib"
		INSTALL_RPATH "@executable_path"
	)
elseif(UNIX)
	set_target_properties(hlc_app PROPERTIES
		BUILD_RPATH "$ORIGIN;${HASHLINK};${HASHLINK}/lib"
		INSTALL_RPATH "$ORIGIN"
	)
endif()

if(HLC_COPY_RUNTIME_MODULES)
	set(_hlc_runtime_files)
	set(_hlc_runtime_names)

	foreach(_dir IN LISTS HLC_NATIVE_SEARCH_DIRS)
		if(NOT IS_DIRECTORY "${_dir}")
			continue()
		endif()

		file(GLOB _hlc_candidates CONFIGURE_DEPENDS
			"${_dir}/*.hdll"
			"${_dir}/*.limen"
		)

		if(WIN32)
			file(GLOB _hlc_platform_candidates CONFIGURE_DEPENDS
				"${_dir}/*.dll"
			)
		elseif(APPLE)
			file(GLOB _hlc_platform_candidates CONFIGURE_DEPENDS
				"${_dir}/*.dylib"
			)
		elseif(UNIX)
			file(GLOB _hlc_platform_candidates CONFIGURE_DEPENDS
				"${_dir}/*.so"
				"${_dir}/*.so.*"
			)
		else()
			set(_hlc_platform_candidates)
		endif()

		list(APPEND _hlc_candidates ${_hlc_platform_candidates})

		foreach(_file IN LISTS _hlc_candidates)
			get_filename_component(_runtime_name "${_file}" NAME)
			if(NOT _runtime_name IN_LIST _hlc_runtime_names)
				list(APPEND _hlc_runtime_names "${_runtime_name}")
				list(APPEND _hlc_runtime_files "${_file}")
			endif()
		endforeach()
	endforeach()

	if(_hlc_runtime_files)
		add_custom_command(TARGET hlc_app POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different
				${_hlc_runtime_files}
				"$<TARGET_FILE_DIR:hlc_app>"
			COMMAND_EXPAND_LISTS
			VERBATIM
		)
	endif()
endif()
