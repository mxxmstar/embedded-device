add_library(common_headers INTERFACE) # 头文件库
add_library(Common::Headers ALIAS common_headers)
target_include_directories(common_headers INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

add_library(common_lib STATIC ${EMBEDDED_PROCESS_SOURCE})
add_library(Common::Common ALIAS common_lib)
target_link_libraries(common_lib PUBLIC Common::Headers)
target_link_libraries(common_lib PUBLIC Platform)
target_compile_options(common_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>)
if(MSVC)
    target_compile_options(common_lib PUBLIC /utf-8 /Zc:__cplusplus)
endif()

if(BUILD_TESTS)
    add_subdirectory(process/test)
endif()
