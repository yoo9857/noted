add_executable(noted_unit_tests
    error_test.cpp
    hook_test.cpp
    harness_test.cpp
    engine_test.cpp
    stroke_engine_test.cpp
)

target_link_libraries(noted_unit_tests PRIVATE noted_test_harness)
target_compile_features(noted_unit_tests PRIVATE cxx_std_23)
noted_apply_warnings(noted_unit_tests)

gtest_discover_tests(noted_unit_tests)
