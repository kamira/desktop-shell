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

# -----------------------------------------------------------------------------
# 警告旗標（CHG-20261112-04）
#
# **為什麼插在這個位置，而不是檔頭。** `add_compile_options` 設的是目錄屬性，只在目標
# **被建立時**被讀取；googletest 的目標在上面 `FetchContent_MakeAvailable` 當場就建立完了。
# 放在它之後、放在下面收各單元 CMakeLists 的 foreach 之前，旗標就只套到本庫自己的目標——
# 日後升 googletest 版本冒出的是別人的警告，不該由本庫的 `-Werror` 承擔。
#
# **為什麼需要它。** 在本區塊出現之前，全 repo `-Wall|-Wextra|-Werror|/W[0-9]` grep 為
# 0 命中，實際 `CXX_FLAGS` 只有 `-std=gnu++17`。於是每一份 CHG 寫的「建置零警告」
# 幾近自動成立，不管程式碼怎麼寫（`CHG-20261112-02` 已載明此缺口）。
#
# **實測（g++ 13.3.0，加入當時）**：全 repo 在 `-Wall -Wextra` 下只有 **3** 個警告，
# 全部是 `tests/e7/test_e7_09.cpp` 的 `-Wdangling-reference`，且**全為誤判**——
# 該檔的 `child(r, key)` 回傳 `r.value.at(key)` 的參考，指向具名區域物件 `r`
# （三個站點都是 `DegradeResult r = resolve_with_fallback(...)`），GCC 只是無法證明
# 它不指向 `key` 那個臨時字串。該測試在 ASan 下乾淨通過、零報告。
#
# 故 `DS_WERROR=ON` 時以 `-Wno-error=dangling-reference` 放行這一類：警告**仍會印出來**
# （日後真有一個會看得見），但不擋紅，也不必為了開旗標去動已合併單元的測試。
#
# `-Werror` 不放進預設建置、只由 CI 的 G3 以 `-DDS_WERROR=ON` 開啟：編譯器升版會新增
# 警告種類，把 `-Werror` 寫死在預設建置等於讓任何人換個編譯器就建不起來。
#
# MSVC **刻意不加 `/W4`**：本變更是在 Linux 上做的，無法實測 MSVC 下會冒出多少警告，
# 未經實測就打開等於替 `gate_windows` 製造一道不知深淺的紅燈。另案處理。
# -----------------------------------------------------------------------------
option(DS_WERROR "把警告升級為錯誤（CI 的 G3 以 -DDS_WERROR=ON 開啟）" OFF)
if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
  if(DS_WERROR)
    add_compile_options(-Werror)
    # -Wdangling-reference 是 GCC 13 才有的旗標；clang 不認得，而在 -Werror 之下
    # 一個不認得的 -Wno-error= 會自己變成錯誤（-Wunknown-warning-option）。故限定 GNU。
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      add_compile_options(-Wno-error=dangling-reference)
    endif()
  endif()
endif()

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
