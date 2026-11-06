#include "media/encoder/encoder_config.h"







bool EncoderConfig::is_video() const {
    return media_type == MediaType::VIDEO;
}

bool EncoderConfig::is_audio() const {
    return media_type == MediaType::AUDIO;
}

AudioEncoderConfig& EncoderConfig::audio() {
    return std::get<AudioEncoderConfig>(specific);
}

VideoEncoderConfig& EncoderConfig::video() {
    return std::get<VideoEncoderConfig>(specific);
}

const AudioEncoderConfig& EncoderConfig::audio() const {
    if (is_audio()) {
        return std::get<AudioEncoderConfig>(specific);
    }
    static AudioEncoderConfig empty;
    return empty;
}

const VideoEncoderConfig& EncoderConfig::video() const {
    if (is_video()) {
        return std::get<VideoEncoderConfig>(specific);
    }
    static VideoEncoderConfig empty;
    return empty;
}

bool VideoEncoderConfig::is_valid() const {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (fps_num <= 0 || fps_den <= 0) {
        return false;
    }
    if (gop_size <= 0 || max_b_frames < 0) {
        return false;
    }
    return true;
}

bool EncoderConfig::is_valid() const {
    if (bitrate <= 0) {
        return false;
    }
    // 时间基要么不指定（由 fps/sample_rate 推导），要么分子分母同时为正。
    if (time_base_num < 0 || time_base_den < 0) {
        return false;
    }
    if ((time_base_num > 0) != (time_base_den > 0)) {
        return false;
    }
    if (media_type == MediaType::AUDIO) {
        if (!std::get_if<AudioEncoderConfig>(&specific)) {
            return false;
        }
        if (audio().sample_rate <= 0 || audio().channels <= 0) {
            return false;
        }
    } else if (media_type == MediaType::VIDEO) {
        if (!std::get_if<VideoEncoderConfig>(&specific)) {
            return false;
        }
        if (!video().is_valid()) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

bool EncodedTrackInfo::is_valid() const {
    if (media_type == MediaType::AUDIO) {
        if (audio().sample_rate <= 0 || audio().channels <= 0) {
            return false;
        }
    } else if (media_type == MediaType::VIDEO) {
        if (video().width <= 0 || video().height <= 0) {
            return false;
        }
        if (video().fps <= 0) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

bool EncodedTrackInfo::is_video() const {
    return media_type == MediaType::VIDEO;
}
bool EncodedTrackInfo::is_audio() const {
    return media_type == MediaType::AUDIO;
}

AudioTrackInfo& EncodedTrackInfo::audio() {
    return std::get<AudioTrackInfo>(specific);
}

VideoTrackInfo& EncodedTrackInfo::video() {
    return std::get<VideoTrackInfo>(specific);
}

const AudioTrackInfo& EncodedTrackInfo::audio() const {
    if (is_audio()) {
        return std::get<AudioTrackInfo>(specific);
    }
    static AudioTrackInfo empty;
    return empty;
}
const VideoTrackInfo& EncodedTrackInfo::video() const {
    if (is_video()) {
        return std::get<VideoTrackInfo>(specific);
    }
    static VideoTrackInfo empty;
    return empty;
}