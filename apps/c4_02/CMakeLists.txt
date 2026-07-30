# apps/c4_02/CMakeLists.txt — C4-02 截圖與標註
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_02 STATIC screenshot_annotator.cpp)
target_include_directories(c4_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e3_11：螢幕擷取致動器（ScreenCaptureBackend / CaptureSpec / CaptureRegion / CaptureResult），
#     供 capture() 以注入式後端擷取指定區域。
#   - e4_30：全螢幕調光覆蓋（DimOverlayElement），借它組出選取框覆蓋。
#   - e4_20：向量圖形（VectorGraphic / Shape / build_shape_path），供 annotate_arrow /
#     annotate_box 把標註轉為向量渲染描述。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_02 PUBLIC e3_11 e4_30 e4_20)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_02 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_02.cpp)
target_link_libraries(test_c4_02 PRIVATE c4_02 GTest::gtest_main)
add_test(NAME c4_02 COMMAND test_c4_02)
