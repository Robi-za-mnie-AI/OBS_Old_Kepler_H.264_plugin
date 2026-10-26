cmake_minimum_required(VERSION 3.28...3.30)

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR "nvenc old kepler is a Windows-only plugin.")
endif()

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)
option(ENABLE_HEVC "Enable HEVC encoders" OFF)
option(LEGACY_NVENC_API "Build H.264-only against NVIDIA NVENC API 8.1 (legacy drivers)" OFF)

if(NOT LEGACY_NVENC_API)
  message(FATAL_ERROR "This Windows-only build requires LEGACY_NVENC_API=ON (NVENC API 8.1, H.264 only).")
endif()
set(ENABLE_HEVC OFF CACHE BOOL "Legacy API 8.1 supports H.264 only in this port" FORCE)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/obs-nvenc.c)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA

if(NOT TARGET OBS::opts-parser)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/opts-parser" "${CMAKE_BINARY_DIR}/shared/opts-parser")
endif()

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common")

if(LEGACY_NVENC_API)
  # Pin the legacy build to the exact NVENC API 8.1 headers. Do not rely on
  # whatever FFnvcodec version happens to be installed in obs-deps.
  set(_nvenc_legacy_headers_version "n8.1.24.15")
  set(_nvenc_deps_dir "${CMAKE_BINARY_DIR}/_deps/nv-codec-headers-${_nvenc_legacy_headers_version}")
  set(_nvenc_archive "${_nvenc_deps_dir}.tar.gz")
  set(_nvenc_extract_dir "${_nvenc_deps_dir}-src")

  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
  if(NOT EXISTS "${_nvenc_archive}")
    message(STATUS "Downloading pinned NVENC headers ${_nvenc_legacy_headers_version}")
    file(DOWNLOAD
      "https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/${_nvenc_legacy_headers_version}.tar.gz"
      "${_nvenc_archive}"
      TLS_VERIFY ON
      TIMEOUT 120
      INACTIVITY_TIMEOUT 30
      STATUS _nvenc_download_status
      LOG _nvenc_download_log
    )
    list(GET _nvenc_download_status 0 _nvenc_download_code)
    if(NOT _nvenc_download_code EQUAL 0)
      message(FATAL_ERROR
        "Could not download pinned NVENC headers ${_nvenc_legacy_headers_version}: ${_nvenc_download_status}\n${_nvenc_download_log}")
    endif()
  endif()

  file(GLOB _nvenc_existing_headers
    "${_nvenc_extract_dir}/*/include/ffnvcodec/nvEncodeAPI.h")
  if(NOT _nvenc_existing_headers)
    file(MAKE_DIRECTORY "${_nvenc_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_nvenc_archive}" DESTINATION "${_nvenc_extract_dir}")
    file(GLOB _nvenc_existing_headers
      "${_nvenc_extract_dir}/*/include/ffnvcodec/nvEncodeAPI.h")
  endif()

  list(LENGTH _nvenc_existing_headers _nvenc_header_count)
  if(NOT _nvenc_header_count EQUAL 1)
    message(FATAL_ERROR
      "Expected exactly one pinned ffnvcodec/nvEncodeAPI.h under ${_nvenc_extract_dir}; found ${_nvenc_header_count}.")
  endif()

  list(GET _nvenc_existing_headers 0 _nvenc_header_file)
  get_filename_component(_nvenc_ffnvcodec_dir "${_nvenc_header_file}" DIRECTORY)
  get_filename_component(_nvenc_include_dir "${_nvenc_ffnvcodec_dir}" DIRECTORY)

  file(STRINGS "${_nvenc_header_file}" _nvenc_major_line
    REGEX "^[ \t]*#define[ \t]+NVENCAPI_MAJOR_VERSION[ \t]+[0-9]+")
  file(STRINGS "${_nvenc_header_file}" _nvenc_minor_line
    REGEX "^[ \t]*#define[ \t]+NVENCAPI_MINOR_VERSION[ \t]+[0-9]+")
  string(REGEX MATCH "[0-9]+" _nvenc_major "${_nvenc_major_line}")
  string(REGEX MATCH "[0-9]+" _nvenc_minor "${_nvenc_minor_line}")

  if(NOT _nvenc_major STREQUAL "8" OR NOT _nvenc_minor STREQUAL "1")
    message(FATAL_ERROR
      "LEGACY_NVENC_API requires exactly NVENC API 8.1; pinned header reports ${_nvenc_major}.${_nvenc_minor} (${_nvenc_header_file}).")
  endif()

  if(NOT TARGET FFnvcodec::FFnvcodec)
    add_library(FFnvcodec::FFnvcodec INTERFACE IMPORTED GLOBAL)
    set_target_properties(FFnvcodec::FFnvcodec PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_nvenc_include_dir}")
  endif()
  # Put the pinned headers ahead of any transitive obs-deps include paths.
  target_include_directories(${CMAKE_PROJECT_NAME} BEFORE PRIVATE "${_nvenc_include_dir}")
  message(STATUS "LEGACY NVENC headers: API ${_nvenc_major}.${_nvenc_minor} from ${_nvenc_header_file}")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:src/nvenc-d3d11.c>
    src/cuda-helpers.c
    src/cuda-helpers.h
    src/nvenc-compat.c
    src/nvenc-cuda.c
    src/nvenc-helpers.c
    src/nvenc-helpers.h
    src/nvenc-internal.h
    src/nvenc-opts-parser.c
    src/nvenc-properties.c
    src/nvenc.c
    src/obs-nvenc.c
    src/obs-nvenc.h
)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::opts-parser FFnvcodec::FFnvcodec
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE $<$<BOOL:${ENABLE_NVENC_FFMPEG_IDS}>:REGISTER_FFMPEG_IDS> $<$<BOOL:${LEGACY_NVENC_API}>:LEGACY_NVENC_API>)
