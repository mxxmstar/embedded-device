add_executable(test_mpp_encoder test_mpp_encoder.cpp)
target_link_libraries(test_mpp_encoder PRIVATE Media::Encoder)
set_target_properties(test_mpp_encoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/tests"
    SKIP_BUILD_RPATH TRUE)
# Requires a real Rockchip board. Do not register a runnable host CTest when cross compiling.
if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME test_mpp_encoder COMMAND test_mpp_encoder)
    set_tests_properties(test_mpp_encoder PROPERTIES TIMEOUT 30)
endif()
