#pragma once

#include "ai/model.hpp"

namespace gai {

struct EmbeddingRequest : Model<EmbeddingRequest> {
    std::string              model;
    std::vector<std::string> input;
    template <class V> void fields(V&& v) {
        v("model", model);
        v("input", input);
    }
};

struct EmbeddingRow : Model<EmbeddingRow> {
    std::vector<f64> embedding;
    i64              index{0};
    template <class V> void fields(V&& v) {
        v("embedding", embedding);
        v("index", index);
    }
};

struct EmbeddingResponse : Model<EmbeddingResponse> {
    std::vector<EmbeddingRow> data;
    template <class V> void fields(V&& v) { v("data", data); }
};

}
