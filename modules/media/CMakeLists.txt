# 每个 media 子模块的头文件都包含在 media_contracts 中，用于编译时检查依赖
add_library(media_contracts INTERFACE)
add_library(Media::Contracts ALIAS media_contracts)
foreach(module core capture converter decoder encoder puller pusher publisher stream)
    target_include_directories(media_contracts INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${module}/include>")
endforeach()
target_link_libraries(media_contracts INTERFACE Common::Headers)

# media 模块公共代码
add_subdirectory(core)
add_library(media_modules INTERFACE)
add_library(Media::All ALIAS media_modules)
target_link_libraries(media_modules INTERFACE Media::Core)

# media 模块公共函数，用于创建 media 模块的静态库
function(media_module name)
    add_library(${name} STATIC ${ARGN})
    target_link_libraries(${name} PUBLIC Media::Core)
    target_include_directories(${name} PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backend/include>")
    target_compile_options(${name} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endfunction()

# x86 平台和 rockchip 平台的 media 目录
if(MEDIA_FFMPEG_FULL OR MEDIA_FFMPEG_ROCKCHIP)
    add_subdirectory(ffmpeg)
    add_subdirectory(diagnostics)
    if(MEDIA_FFMPEG_FULL)
        add_subdirectory(converter)
    endif()
    add_subdirectory(encoder)
    add_subdirectory(pusher)
    add_subdirectory(publisher)
    target_link_libraries(media_modules INTERFACE Media::Output Media::Encoder Media::Publisher)
endif()

# 非 x86 平台的 media 模块处理
if(NOT MEDIA_FFMPEG_FULL)
    # V4L2 采集只依赖 Media::Core、spdlog 和 Threads，不依赖 FFmpeg，
    # 因此在 rockchip 平台下同样可用，供 ISP 示例直接链接 Media::Capture。
    if(BUILD_V4L2_CAPTURE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # BUILD_V4L2_CAPTURE 独立查找 spdlog
        if(NOT TARGET spdlog::spdlog)
            find_package(spdlog CONFIG REQUIRED)
        endif()
        add_subdirectory(capture)
    endif()
    if(BUILD_TESTS AND BUILD_MEDIA_TESTS)
        add_subdirectory(core/test)
        if(MEDIA_FFMPEG_ROCKCHIP)
            include("${EMBEDDED_SOURCE_ROOT}/cmake/MediaTests.cmake")
            add_subdirectory(pusher/test)
        endif()
    endif()
    return()
endif()

# 完整的 media 模块处理，包括 capture、converter、encoder、decoder、puller、publisher、stream 等子模块
foreach(module decoder puller stream)
    add_subdirectory(${module})
endforeach()
foreach(module converter encoder decoder input output publisher pipeline)
    target_link_libraries(media_modules INTERFACE media_${module})
endforeach()
if(BUILD_V4L2_CAPTURE)
    add_subdirectory(capture)
    target_link_libraries(media_modules INTERFACE Media::Capture)
endif()

# media 模块测试代码
if(BUILD_TESTS AND BUILD_MEDIA_TESTS)
    include("${EMBEDDED_SOURCE_ROOT}/cmake/MediaTests.cmake")
    foreach(module core ffmpeg converter pusher publisher stream pipeline)
        add_subdirectory(${module}/test)
    endforeach()
    if(BUILD_V4L2_CAPTURE)
        add_subdirectory(capture/test)
    endif()
endif()