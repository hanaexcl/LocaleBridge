cmake_minimum_required(VERSION 3.21)
project(LocaleBridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(FATAL_ERROR "LocaleBridge 只支援 MSVC（Windows）。")
endif()

# 靜態連結 CRT，讓產物不依賴 VC++ Redistributable，注入時更省事
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 目標 Windows 10+（IsWow64Process2 等 API 需要），並精簡 windows.h
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)

# 依位元決定產物名稱：LocaleHook32/64、LocaleLoader32/64
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH_SUFFIX "64")
else()
  set(ARCH_SUFFIX "32")
endif()

# --- MinHook（inline hook 引擎）---
include(FetchContent)
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.3
)
FetchContent_MakeAvailable(minhook)

# --- 共用 ---
add_library(le_common INTERFACE)
target_include_directories(le_common INTERFACE ${CMAKE_SOURCE_DIR}/src)

# --- Hook DLL ---
file(GLOB HOOK_SRC ${CMAKE_SOURCE_DIR}/src/hook/*.cpp ${CMAKE_SOURCE_DIR}/src/common/*.cpp)
add_library(LocaleHook SHARED ${HOOK_SRC})
set_target_properties(LocaleHook PROPERTIES OUTPUT_NAME "LocaleHook${ARCH_SUFFIX}")
target_link_libraries(LocaleHook PRIVATE le_common minhook gdi32 user32)
# MinHook 這個 tag 的 CMakeLists 沒把 include 目錄設成 PUBLIC，明確補上
target_include_directories(LocaleHook PRIVATE ${minhook_SOURCE_DIR}/include)

# --- Loader EXE ---
file(GLOB LOADER_SRC ${CMAKE_SOURCE_DIR}/src/loader/*.cpp ${CMAKE_SOURCE_DIR}/src/common/*.cpp)
add_executable(LocaleLoader ${LOADER_SRC})
set_target_properties(LocaleLoader PROPERTIES OUTPUT_NAME "LocaleLoader${ARCH_SUFFIX}")
target_link_libraries(LocaleLoader PRIVATE le_common user32)
