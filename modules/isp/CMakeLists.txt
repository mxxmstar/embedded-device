# Independent of BUILD_MEDIA, FFmpeg, capture, and rendering.
if(NOT EMBEDDED_PLATFORM STREQUAL "rockchip" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    message(FATAL_ERROR "BUILD_ISP currently implements the NanoPC-T4 RK3399 ARM64 legacy backend only")
endif()
set(RK3399_ISP_SDK_ROOT "${ROCKCHIP_DEPS_ROOT}/rkisp" CACHE PATH "Exported NanoPC-T4 RKISP 2.3.0 SDK overlay")
find_path(RK3399_ISP_INCLUDE_DIR rkisp_control_loop.h
    PATHS "${RK3399_ISP_SDK_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(RK3399_ISP_LIBRARY NAMES rkisp
    PATHS "${RK3399_ISP_SDK_ROOT}/rootfs/usr/lib" NO_DEFAULT_PATH REQUIRED)
add_library(rk3399_isp_vendor SHARED IMPORTED)
set_target_properties(rk3399_isp_vendor PROPERTIES
    IMPORTED_LOCATION "${RK3399_ISP_LIBRARY}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${RK3399_ISP_INCLUDE_DIR}")
add_library(isp_controller STATIC
    src/isp_controller.cpp src/rockchip/rk3399_isp.cpp src/rockchip/rk3399_topology.cpp)
add_library(Isp::Controller ALIAS isp_controller)
target_include_directories(isp_controller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(isp_controller PRIVATE rk3399_isp_vendor Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(isp_controller PRIVATE -Wall -Wextra -Wpedantic)
add_executable(isp_probe examples/isp_probe.cpp)
target_link_libraries(isp_probe PRIVATE Isp::Controller)
# Do not embed build-host SDK paths in deployed binaries.
set_target_properties(isp_probe PROPERTIES SKIP_BUILD_RPATH TRUE)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
