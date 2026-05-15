include(GoogleTest)

# Common test harness library used by every test target.
add_library(noted_test_harness STATIC
    harness/harness.cpp
)
target_include_directories(noted_test_harness PUBLIC harness)
target_link_libraries(noted_test_harness PUBLIC
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    noted::engine
)
target_compile_features(noted_test_harness PUBLIC cxx_std_23)
noted_apply_warnings(noted_test_harness)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(bench)
add_subdirectory(fuzz)
