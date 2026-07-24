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

# 自動納入各工作單元的 CMakeLists（platform=src/、engine/、modules/）。
# 單元子目錄多為 <top>/<subsystem>/<unit_slug>/（深度 2），後端如 src/kernel/backend/null/（深度 3）。
# 用明確層級的 GLOB（每個 * = 一段路徑）涵蓋深度 2 與 3，穩定不誤收。
file(GLOB _unit_cmakes
  "${CMAKE_SOURCE_DIR}/src/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/src/*/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/engine/*/*/CMakeLists.txt"
  "${CMAKE_SOURCE_DIR}/modules/*/*/CMakeLists.txt")

foreach(_cml ${_unit_cmakes})
  get_filename_component(_dir "${_cml}" DIRECTORY)
  file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_dir}")
  add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/units/${_rel}")
endforeach()

list(LENGTH _unit_cmakes _n)
message(STATUS "desktop-shell: 收入 ${_n} 個工作單元 CMakeLists")
