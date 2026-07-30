# desktop-shell 根建置檔（bootstrap 基礎設施，非工作單元）
#
# 每個工作單元在自己的 write_scope 目錄內放一個 CMakeLists.txt（見 docs/backlog/BUILD.md），
# 根層自動 glob 收入 —— 單元不需、也不得改本檔（本檔不在任何單元的 write_scope 內）。
cmake_minimum_required(VERSION 3.16)
project(desktop_shell LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  "${CMAKE_SOURCE_DIR}/src/*/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/engine/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/modules/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/tests/contract/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/tests/contract/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/content/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/content/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/apps/CMakeLists.txt"
  # examples/：最終交付的驗證器（組裝擴充點、不改 src/），如 examples/cpu_gpu_validator/。
  "${CMAKE_SOURCE_DIR}/examples/*/CMakeLists.txt")

foreach(_cml ${_unit_cmakes})
  get_filename_component(_dir "${_cml}" DIRECTORY)
  file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_dir}")
  add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/units/${_rel}")
endforeach()

list(LENGTH _unit_cmakes _n)
message(STATUS "desktop-shell: 收入 ${_n} 個工作單元 CMakeLists")
