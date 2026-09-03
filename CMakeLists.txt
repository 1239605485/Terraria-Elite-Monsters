cmake_minimum_required(VERSION 3.22.1)

project("OriginRewrite" LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(ORIGINREWRITE_MOD_API_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mod-api"
    CACHE PATH "TEFKernel mod-api directory")

if (NOT EXISTS "${ORIGINREWRITE_MOD_API_DIR}/mod_core.h")
    message(FATAL_ERROR
        "TEFKernel mod-api not found. Set ORIGINREWRITE_MOD_API_DIR to the mod-api directory.")
endif()

set(ORIGINREWRITE_FRAMEWORK_SOURCES
    src/fw_mod.c
    src/fw_core.c
    src/fw_roll.c
    src/fw_runtime.c
    "${ORIGINREWRITE_MOD_API_DIR}/tefkernel/tef_api_imp.c"
)

add_library(OriginRewrite SHARED ${ORIGINREWRITE_FRAMEWORK_SOURCES})

target_include_directories(OriginRewrite
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${ORIGINREWRITE_MOD_API_DIR}"
)

target_compile_definitions(OriginRewrite PRIVATE ORIGINREWRITE_BUILDING=1 BUILDING_DLL=1)

if (UNIX AND NOT APPLE)
    target_link_libraries(OriginRewrite PRIVATE m)
endif()

if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(OriginRewrite PRIVATE
        ORIGINREWRITE_USE_ANDROID_LOG=1)
    target_link_libraries(OriginRewrite PRIVATE log)
endif()

if (WIN32)
    set_target_properties(OriginRewrite PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
elseif (APPLE)
    set_target_properties(OriginRewrite PROPERTIES SUFFIX ".dylib")
else()
    set_target_properties(OriginRewrite PROPERTIES SUFFIX ".so")
endif()

if (ANDROID_ABI STREQUAL "arm64-v8a")
    set(ORIGINREWRITE_ARCH "arm64")
elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
    set(ORIGINREWRITE_ARCH "arm")
elseif (ANDROID_ABI STREQUAL "x86_64")
    set(ORIGINREWRITE_ARCH "x64")
elseif (ANDROID_ABI STREQUAL "x86")
    set(ORIGINREWRITE_ARCH "x86")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(ORIGINREWRITE_ARCH "x64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86")
    set(ORIGINREWRITE_ARCH "x86")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(ORIGINREWRITE_ARCH "arm64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|armv7a|ARMv7|arm")
    set(ORIGINREWRITE_ARCH "arm")
elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ORIGINREWRITE_ARCH "x64")
else()
    set(ORIGINREWRITE_ARCH "x86")
endif()

set_target_properties(OriginRewrite PROPERTIES
    OUTPUT_NAME "OriginRewrite.android.${ORIGINREWRITE_ARCH}"
    POSITION_INDEPENDENT_CODE ON
)
