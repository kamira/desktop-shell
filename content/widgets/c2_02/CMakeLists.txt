# content/widgets/c2_02/CMakeLists.txt — C2-02 系統狀態 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_02 STATIC system_status_widget.cpp)
target_include_directories(c2_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝七個擴充點——
#   - c1_01（C1-01 桌面角色皮膚基底 profile）：作為本 widget 的桌面基底，圖層 / 輸入策略 /
#     透明外形 / 自由拖曳皆透傳，本單元不重造。
#   - e2_01（統一指標介面）：唯一的指標讀取入口——消費者只依賴 MetricRegistry::get() /
#     Metric / MetricInstance 抽象契約，不寫死任何具體感測器（CPU/GPU/RAM/…皆可選）。
#   - e2_02（採集頻率分級與除頻）：sync_demands() 依各指標的宣告式 tier 對 SamplingScheduler
#     登記取樣需求（除頻合併）。
#   - e4_01（文字排版與渲染）：把每筆「標籤: 格式化值」文字排版為渲染描述（TextLayout）。
#   - e4_03（長條 / 進度 / 量表）：把指標值依範圍組裝為 Bar/Gauge/Progress 渲染描述。
#   - e7_03（段落變數）：build_scope() 由宣告式設定的 vars: 段落建立具名常數作用域。
#   - e7_05（公式運算引擎）：min/max 靜態公式 + 每指標 transform 動態公式（value 變數綁定）求值。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_02 PUBLIC c1_01 e2_01 e2_02 e4_01 e4_03 e7_03 e7_05)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_02 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_02.cpp)
target_link_libraries(test_c2_02 PRIVATE c2_02 GTest::gtest_main)
add_test(NAME c2_02 COMMAND test_c2_02)
