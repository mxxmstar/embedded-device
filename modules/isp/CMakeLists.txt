# 校验平台是否为 rockchip 平台，且处理器是否为 aarch64 或 arm64
if(NOT EMBEDDED_PLATFORM STREQUAL "rockchip" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    message(FATAL_ERROR "BUILD_ISP currently implements the NanoPC-T4 RK3399 ARM64 legacy backend only")
endif()

# RK3399 SDK 相关配置
set(RK3399_ISP_SDK_ROOT "${ROCKCHIP_DEPS_ROOT}/rkisp" CACHE PATH "Exported NanoPC-T4 RKISP 2.3.0 SDK overlay")
find_path(RK3399_ISP_INCLUDE_DIR rkisp_control_loop.h
    PATHS "${RK3399_ISP_SDK_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(RK3399_ISP_LIBRARY NAMES rkisp
    PATHS "${RK3399_ISP_SDK_ROOT}/rootfs/usr/lib" NO_DEFAULT_PATH REQUIRED)

# 导入 vendor 库，用于链接 RK3399 ISP 库文件
add_library(rk3399_isp_vendor SHARED IMPORTED)
set_target_properties(rk3399_isp_vendor PROPERTIES
    IMPORTED_LOCATION "${RK3399_ISP_LIBRARY}"   # 仅构建时链接
    IMPORTED_NO_SONAME TRUE # 运行时加载板端的 ISP 库文件
    INTERFACE_INCLUDE_DIRECTORIES "${RK3399_ISP_INCLUDE_DIR}")   # 包含 ISP 头文件目录

# 创建 isp_controller 静态库
add_library(isp_controller STATIC
    src/isp_controller.cpp src/rockchip/rk3399_isp.cpp src/rockchip/rk3399_topology.cpp)
add_library(Isp::Controller ALIAS isp_controller)
target_include_directories(isp_controller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(isp_controller PRIVATE rk3399_isp_vendor Threads::Threads ${CMAKE_DL_LIBS})

# 处理 RK3399 的 rkisp 特殊情况
if(TARGET media_capture)
    target_compile_definitions(media_capture PRIVATE MEDIA_CAPTURE_RKISP=1)
endif()

target_compile_options(isp_controller PRIVATE -Wall -Wextra -Wpedantic)
add_executable(isp_probe examples/isp_probe.cpp)
target_link_libraries(isp_probe PRIVATE Isp::Controller)
# Do not embed build-host SDK paths in deployed binaries.
set_target_properties(isp_probe PROPERTIES SKIP_BUILD_RPATH TRUE)

# ISP + V4L2 采集示例依赖 Media::Capture（由 modules/media 在 BUILD_V4L2_CAPTURE 时提供）。
# media 目录在顶层先于 isp 配置，因此这里可以直接用 TARGET 判断。
if(TARGET Media::Capture)
    add_executable(video_capture_loop examples/video_capture_loop.cpp)
    target_link_libraries(video_capture_loop PRIVATE Isp::Controller Media::Capture)
    set_target_properties(video_capture_loop PROPERTIES SKIP_BUILD_RPATH TRUE)
else()
    message(STATUS "video_capture_loop disabled: Media::Capture not built "
        "(需要 BUILD_MEDIA=ON 且 BUILD_V4L2_CAPTURE=ON)")
endif()

if(BUILD_TESTS)
    add_subdirectory(test)
endif()