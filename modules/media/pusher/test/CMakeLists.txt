media_test(test_async_pusher_session media_pusher_session)
media_test(test_ffmpeg_pusher Media::Output)
media_test(test_pusher_session media_pusher_session)

# Concrete backend access is restricted to this implementation test.
target_include_directories(test_media_test_ffmpeg_pusher PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/ffmpeg")
