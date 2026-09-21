cmake_minimum_required(VERSION 3.21)
project(LocaleBridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(FATAL_ERROR "LocaleBridge 只支援 MSVC（Windows）。")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX
                        _CRT_SECURE_NO_WARNINGS)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH_SUFFIX "64")
else()
  set(ARCH_SUFFIX "32")
endif()

# --- Microsoft Detours（注入 + hook 引擎，與 LR 相同）---
# 用 nmake 版原始碼直接編成 static lib（除 uimports.cpp，它被 creatwth.cpp #include）。
include(FetchContent)
FetchContent_Declare(
  detours
  GIT_REPOSITORY https://github.com/microsoft/Detours.git
  GIT_TAG        v4.0.1
)
FetchContent_GetProperties(detours)
if(NOT detours_POPULATED)
  FetchContent_Populate(detours)
endif()
set(DETOURS_SRC
  ${detours_SOURCE_DIR}/src/detours.cpp
  ${detours_SOURCE_DIR}/src/modules.cpp
  ${detours_SOURCE_DIR}/src/disasm.cpp
  ${detours_SOURCE_DIR}/src/image.cpp
  ${detours_SOURCE_DIR}/src/creatwth.cpp
  ${detours_SOURCE_DIR}/src/disolx86.cpp
  ${detours_SOURCE_DIR}/src/disolx64.cpp
  ${detours_SOURCE_DIR}/src/disolarm.cpp
  ${detours_SOURCE_DIR}/src/disolarm64.cpp
  ${detours_SOURCE_DIR}/src/disolia64.cpp
)
add_library(detours STATIC ${DETOURS_SRC})
target_include_directories(detours PUBLIC ${detours_SOURCE_DIR}/src)
target_compile_options(detours PRIVATE /w)  # Detours 自身警告忽略

add_library(le_common INTERFACE)
target_include_directories(le_common INTERFACE ${CMAKE_SOURCE_DIR}/src)

# --- Hook DLL ---
add_library(LocaleHook SHARED
  ${CMAKE_SOURCE_DIR}/src/hook/hook.cpp
  ${CMAKE_SOURCE_DIR}/src/hook/exports.def)
set_target_properties(LocaleHook PROPERTIES OUTPUT_NAME "LocaleHook${ARCH_SUFFIX}")
target_link_libraries(LocaleHook PRIVATE le_common detours gdi32 user32)

# --- Loader EXE（requireAdministrator）---
add_executable(LocaleLoader
  ${CMAKE_SOURCE_DIR}/src/loader/main.cpp
  ${CMAKE_SOURCE_DIR}/src/loader/loader.manifest)
set_target_properties(LocaleLoader PROPERTIES OUTPUT_NAME "LocaleLoader${ARCH_SUFFIX}")
target_link_libraries(LocaleLoader PRIVATE le_common detours user32)
target_link_options(LocaleLoader PRIVATE "/MANIFESTUAC:NO")
