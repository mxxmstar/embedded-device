find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(render_lib STATIC
    video/opengl_video_renderer.cpp
    video/opengl_dmabuf.cpp)
add_library(Render::Render ALIAS render_lib)
target_include_directories(render_lib PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(render_lib
    PUBLIC Media::Core
    PRIVATE Common::Headers spdlog::spdlog OpenGL::GL glfw glad::glad)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(OpenGL QUIET COMPONENTS EGL)
    if(TARGET OpenGL::EGL)
        target_link_libraries(render_lib PRIVATE OpenGL::EGL)
        target_compile_definitions(render_lib PRIVATE RENDER_EGL_DMABUF=1)
    endif()
endif()

target_compile_options(render_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

if(BUILD_TESTS)
    add_subdirectory(test)
    if(BUILD_V4L2_CAPTURE)
        add_subdirectory("${EMBEDDED_SOURCE_ROOT}/modules/media/capture/test/v4l2-demo"
            "${CMAKE_CURRENT_BINARY_DIR}/v4l2-demo")
    endif()
endif()
