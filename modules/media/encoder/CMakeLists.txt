if(EMBEDDED_PLATFORM STREQUAL "rockchip")
    if(NOT TARGET Rockchip::MPP)
        message(FATAL_ERROR "Rockchip encoder requires ROCKCHIP_WITH_MPP=ON")
    endif()
    media_module(media_encoder src/rockchip/mpp_encoder.cpp)
    target_link_libraries(media_encoder PRIVATE Rockchip::MPP spdlog::spdlog)
else()
    media_module(media_encoder src/generic/ffmpeg_encoder.cpp)
    target_link_libraries(media_encoder PUBLIC media_converter)
endif()
add_library(Media::Encoder ALIAS media_encoder)
target_include_directories(media_encoder PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/generic"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/rockchip")
set_property(TARGET media_encoder PROPERTY INTERFACE_INCLUDE_DIRECTORIES
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

if(BUILD_TESTS AND BUILD_MEDIA_TESTS AND EMBEDDED_PLATFORM STREQUAL "rockchip")
    add_subdirectory(test)
endif()
