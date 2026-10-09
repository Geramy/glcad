#pragma once

#include "ai/model.hpp"

namespace gai {

struct ModelInfo : Model<ModelInfo> {
    std::string               id;
    std::vector<std::string>  labels;
    JsonOptional<std::string> modality;
    JsonOptional<std::string> owned_by;
    JsonOptional<std::string> checkpoint;
    JsonOptional<std::string> recipe;
    JsonOptional<i64>         max_context_window;
    template <class V> void fields(V&& v) {
        v("id", id);
        v("labels", labels);
        v("modality", modality);
        v("owned_by", owned_by);
        v("checkpoint", checkpoint);
        v("recipe", recipe);
        v("max_context_window", max_context_window);
    }

    bool modality_is(std::string_view m) const {
        return modality.has() && modality.get() == m;
    }

    bool has_label(std::string_view l) const {
        for (const auto& x : labels)
            if (x == l) return true;
        return false;
    }
    bool labelled() const noexcept { return !labels.empty(); }
    bool calls_tools() const { return has_label("tool-calling"); }
    bool sees() const { return has_label("vision"); }

    bool embeds() const {
        return modality_is("embedding") || has_label("embedding") ||
               has_label("embeddings");
    }
    bool speaks() const { return modality_is("tts") || has_label("tts"); }
    bool transcribes() const {
        return modality_is("stt") || has_label("stt") ||
               has_label("transcription") || has_label("realtime-transcription");
    }
    bool makes_images() const {
        return modality_is("image") || has_label("image-generation") ||
               has_label("text-to-image") ||
               (has_label("image") && has_label("generation"));
    }
    bool chats() const {
        if (embeds() || speaks() || transcribes() || makes_images()) return false;
        return has_label("chat") || modality_is("text") || has_label("text") ||
               calls_tools();
    }
};

inline bool has_model(const std::vector<ModelInfo>& models,
                      const std::string& want) {
    if (want.empty()) return false;
    for (const auto& m : models)
        if (m.id == want) return true;
    for (const auto& m : models)
        if (m.id.find(want) != std::string::npos) return true;
    return false;
}

struct ModelList : Model<ModelList> {
    std::vector<ModelInfo> data;
    template <class V> void fields(V&& v) { v("data", data); }
};

struct Health : Model<Health> {
    std::string               status;
    JsonOptional<std::string> model_loaded;
    template <class V> void fields(V&& v) {
        v("status", status);
        v("model_loaded", model_loaded);
    }
};

struct Beacon : Model<Beacon> {
    std::string service;
    std::string hostname;
    std::string url;
    template <class V> void fields(V&& v) {
        v("service", service);
        v("hostname", hostname);
        v("url", url);
    }
};

}
