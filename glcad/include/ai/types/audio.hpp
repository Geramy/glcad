#pragma once

#include "ai/model.hpp"

namespace gai {

struct SpeechRequest : Model<SpeechRequest> {
    std::string       model;
    std::string       input;
    std::string       voice{"af_heart"};
    std::string       response_format{"wav"};
    JsonOptional<f64> speed;
    JsonOptional<bool> stream;
    template <class V> void fields(V&& v) {
        v("model", model);
        v("input", input);
        v("voice", voice);
        v("response_format", response_format);
        v("speed", speed);
        v("stream", stream);
    }
};

struct Transcription : Model<Transcription> {
    std::string text;
    template <class V> void fields(V&& v) { v("text", text); }
};

}
