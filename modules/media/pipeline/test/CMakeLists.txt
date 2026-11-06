# Integration contract across input, converter, codec and publisher modules.
media_test(test_ffmpeg_timestamp_handling Media::Encoder Media::Decoder Media::Input Media::Publisher)

media_test(test_ffmpeg_audio_encoder Media::Encoder)

target_include_directories(test_media_test_ffmpeg_timestamp_handling PRIVATE "${EMBEDDED_SOURCE_ROOT}/modules/media/encoder/src/generic")
target_include_directories(test_media_test_ffmpeg_audio_encoder PRIVATE "${EMBEDDED_SOURCE_ROOT}/modules/media/encoder/src/generic")
