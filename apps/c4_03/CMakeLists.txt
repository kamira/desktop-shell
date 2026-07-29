# apps/c4_03/CMakeLists.txt — C4-03 取色器
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_03 STATIC color_picker.cpp)
target_include_directories(c4_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_03 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - e2_27：螢幕像素取樣（PixelSampleSource / PixelColor），供 pick_at() 取色。
#   - e4_30：全螢幕調光覆蓋（DimOverlayElement），借它組出放大鏡 / 取色游標。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_03 PUBLIC e2_27 e4_30)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_03 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_03.cpp)
target_link_libraries(test_c4_03 PRIVATE c4_03 GTest::gtest_main)
add_test(NAME c4_03 COMMAND test_c4_03)
