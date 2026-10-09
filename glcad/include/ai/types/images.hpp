#pragma once

#include "ai/model.hpp"

namespace gai {

struct ImageRequest : Model<ImageRequest> {
    std::string        model;
    std::string        prompt;
    int                n{1};
    std::string        response_format{"b64_json"};
    JsonOptional<int>  width;
    JsonOptional<int>  height;
    JsonOptional<i64>  seed;
    template <class V> void fields(V&& v) {
        v("model", model);
        v("prompt", prompt);
        v("n", n);
        v("response_format", response_format);
        v("width", width);
        v("height", height);
        v("seed", seed);
    }
};

struct ImageData : Model<ImageData> {
    JsonOptional<std::string> b64_json;
    JsonOptional<std::string> url;
    template <class V> void fields(V&& v) {
        v("b64_json", b64_json);
        v("url", url);
    }
};

struct ImageResponse : Model<ImageResponse> {
    std::vector<ImageData> data;
    template <class V> void fields(V&& v) { v("data", data); }
};

}
