cmake_minimum_required(VERSION 3.21)
project(LocaleBridge C CXX)  # 需要 C：MinHook 是 C 原始碼

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(FATAL_ERROR "LocaleBridge 只支援 MSVC（Windows）。")
endif()

# 靜態連結 CRT，讓產物不依賴 VC++ Redistributable，注入時更省事
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 目標 Windows 10+（IsWow64Process2 等 API 需要），並精簡 windows.h
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)

# 原始碼是 UTF-8：讓 MSVC 正確編碼窄/寬字串字面量（否則中文訊息會亂碼）
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)

# 依位元決定產物名稱：LocaleHook32/64、LocaleLoader32/64
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ARCH_SUFFIX "64")
else()
  set(ARCH_SUFFIX "32")
endif()

# --- MinHook（inline hook 引擎）---
# v1.3.3 沒有 CMakeLists，所以只 Populate（純下載），把 .c 直接編進 DLL。
include(FetchContent)
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        v1.3.3
)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
  FetchContent_Populate(minhook)
endif()
file(GLOB MINHOOK_SRC
  ${minhook_SOURCE_DIR}/src/*.c
  ${minhook_SOURCE_DIR}/src/hde/*.c
)
list(LENGTH MINHOOK_SRC _mh_count)
message(STATUS "MinHook: ${_mh_count} source files from ${minhook_SOURCE_DIR}")

# 建成獨立 static lib —— 把 C 原始碼混進 C++ 的 SHARED target，VS generator 不會編。
add_library(minhook STATIC ${MINHOOK_SRC})
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# --- 共用 ---
add_library(le_common INTERFACE)
target_include_directories(le_common INTERFACE ${CMAKE_SOURCE_DIR}/src)

# --- Hook DLL ---
file(GLOB HOOK_SRC ${CMAKE_SOURCE_DIR}/src/hook/*.cpp ${CMAKE_SOURCE_DIR}/src/common/*.cpp)
add_library(LocaleHook SHARED ${HOOK_SRC})
set_target_properties(LocaleHook PROPERTIES OUTPUT_NAME "LocaleHook${ARCH_SUFFIX}")
target_link_libraries(LocaleHook PRIVATE le_common minhook gdi32 user32)

# --- Loader EXE ---
file(GLOB LOADER_SRC ${CMAKE_SOURCE_DIR}/src/loader/*.cpp ${CMAKE_SOURCE_DIR}/src/common/*.cpp)
add_executable(LocaleLoader ${LOADER_SRC} ${CMAKE_SOURCE_DIR}/src/loader/loader.manifest)
set_target_properties(LocaleLoader PROPERTIES OUTPUT_NAME "LocaleLoader${ARCH_SUFFIX}")
target_link_libraries(LocaleLoader PRIVATE le_common user32)
# 關掉 linker 自動產生的 UAC 片段（預設 asInvoker），改由上面附的 loader.manifest 指定
# requireAdministrator —— 目標程式需要管理員權限才能被 CreateProcess 啟動。
target_link_options(LocaleLoader PRIVATE "/MANIFESTUAC:NO")
