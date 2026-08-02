# desktop-shell 根建置檔（bootstrap 基礎設施，非工作單元）
#
# 每個工作單元在自己的 write_scope 目錄內放一個 CMakeLists.txt（見 docs/backlog/BUILD.md），
# 根層自動 glob 收入 —— 單元不需、也不得改本檔（本檔不在任何單元的 write_scope 內）。
cmake_minimum_required(VERSION 3.16)
project(desktop_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 本庫原始碼一律 UTF-8（無 BOM）且註解與字串含中文。GCC / Clang 預設即以 UTF-8 解讀，
# 但 MSVC 預設用「非 Unicode 程式的系統地區設定」那個 ANSI codepage（例如 CP950 / CP932）
# 去讀原始碼——中文位元組會被當成雙位元組前導碼吃掉結尾引號，報 C2001「常數中包含新行字元」。
# /utf-8 同時把來源字集與執行字集設為 UTF-8，是此問題的標準解。
if(MSVC)
  add_compile_options(/utf-8)
endif()

enable_testing()

# GoogleTest（CI 有網路；本地首次建置會下載）
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 自動納入各子系統的 CMakeLists（CHG-20260730-03 目錄扁平化後）。
# 已去除 per-unit <slug>/ 層：各單元源碼直接置於子系統目錄，該目錄一個合併 CMakeLists 定義其下
# 所有單元的 add_library / test target。故 GLOB 深度較扁平化前少一層。
#   - platform：src/<subsystem>/（kernel / events / host，深度1）；後端 src/kernel/backend/null/（深度3，保留）。
#   - engine / modules：<top>/<subsystem>/（深度1）。
#   - artifact：content/（C3，深度0）、content/<profiles|widgets>/（C1/C2，深度1）、apps/（C4，深度0）。
#   - tests/contract/（跨後端契約測試組 E1-25，非子系統，保留原收法）。
file(GLOB _unit_cmakes
  "${CMAKE_SOURCE_DIR}/src/*/CMakeLists.txt"
  # 深度 2：相位 2 起的平台專屬子目錄，如 src/host/win32/（W1-02 系統匣後端）。
  "${CMAKE_SOURCE_DIR}/src/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/src/*/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/engine/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/modules/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/tests/contract/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/tests/contract/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/content/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/content/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/apps/CMakeLists.txt"
  # host/：相位 2 起的 host shell（把擴充點組裝成真的跑起來的桌面 runtime）。
  # 各子目錄自帶 if(WIN32) 守衛，非 Windows 上收得到檔但不產生任何目標。
  "${CMAKE_SOURCE_DIR}/host/*/CMakeLists.txt"
  # examples/：最終交付的驗證器（組裝擴充點、不改 src/），如 examples/cpu_gpu_validator/。
  "${CMAKE_SOURCE_DIR}/examples/*/CMakeLists.txt")

foreach(_cml ${_unit_cmakes})
  get_filename_component(_dir "${_cml}" DIRECTORY)
  file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_dir}")
  add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/units/${_rel}")
endforeach()

list(LENGTH _unit_cmakes _n)
message(STATUS "desktop-shell: 收入 ${_n} 個工作單元 CMakeLists")
