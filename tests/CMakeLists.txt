# kemuri_core 単体テスト (M1 で JS 参照ベクトル一致テストを追加)
add_executable(kemuri_core_tests
    CoreTests.cpp)

target_link_libraries(kemuri_core_tests PRIVATE kemuri_core)

add_test(NAME kemuri_core_tests COMMAND kemuri_core_tests)
