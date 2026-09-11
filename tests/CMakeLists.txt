add_executable(chopper_tests test_main.cpp)
target_link_libraries(chopper_tests PRIVATE chopper_core)
add_test(NAME chopper_tests COMMAND chopper_tests)
