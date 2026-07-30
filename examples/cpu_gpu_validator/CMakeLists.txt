# examples/cpu_gpu_validator/CMakeLists.txt — CPU/GPU Usage Widget 驗證器（最終交付）
# 純組裝擴充點、不改 src/。連結 c2_02（系統狀態 widget）即遞移取得其完整依賴閉包
# （C1-01 / E2-01 / E2-02 / E4-01 / E4-03 / E7-* / kernel / null backend）與 include 目錄。
add_executable(cpu_gpu_validator main.cpp)
target_link_libraries(cpu_gpu_validator PRIVATE c2_02)
target_compile_features(cpu_gpu_validator PRIVATE cxx_std_17)

# 註冊為一個 ctest 測試：驗證器 main() 以 exit code 回報驗收（0 = 三指標皆成功組裝呈現）。
add_test(NAME cpu_gpu_validator COMMAND cpu_gpu_validator)
