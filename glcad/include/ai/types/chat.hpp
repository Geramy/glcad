#pragma once

#include "ai/types/tools.hpp"

namespace gai {

struct ImageUrl : Model<ImageUrl> {
    std::string url;
    template <class V> void fields(V&& v) { v("url", url); }
};

struct InputAudio : Model<InputAudio> {
    std::string data;
    std::string format{"wav"};
    template <class V> void fields(V&& v) {
        v("data", data);
        v("format", format);
    }
};

struct ContentPart : Model<ContentPart> {
    std::string              type;
    JsonOptional<std::string> text;
    JsonOptional<ImageUrl>    image_url;
    JsonOptional<InputAudio>  input_audio;
    template <class V> void fields(V&& v) {
        v("type", type);
        v("text", text);
        v("image_url", image_url);
        v("input_audio", input_audio);
    }

    static ContentPart of_text(std::string t) {
        ContentPart p; p.type = "text"; p.text = std::move(t); return p;
    }
    static ContentPart of_image(std::string url) {
        ContentPart p; p.type = "image_url"; p.image_url.emplace().url = std::move(url); return p;
    }
    static ContentPart of_audio(std::string base64, std::string format) {
        ContentPart p; p.type = "input_audio";
        auto& a = p.input_audio.emplace();
        a.data = std::move(base64);
        a.format = std::move(format);
        return p;
    }
};

// `content` is a string or a parts array on the wire; the codec is hand-written.
struct ChatMessage : Model<ChatMessage> {
    std::string              role{"user"};
    std::string              content;
    std::vector<ContentPart> parts;
    std::vector<ToolCall>    tool_calls;
    JsonOptional<std::string> tool_call_id;
    JsonOptional<std::string> name;

    static ChatMessage text(std::string role_, std::string content_) {
        ChatMessage m; m.role = std::move(role_); m.content = std::move(content_); return m;
    }
    static ChatMessage tool_result(std::string call_id, std::string result) {
        ChatMessage m;
        m.role = "tool";
        m.tool_call_id = std::move(call_id);
        m.content = std::move(result);
        return m;
    }
    static ChatMessage assistant_calls(std::vector<ToolCall> calls, std::string content_ = {}) {
        ChatMessage m;
        m.role = "assistant";
        m.tool_calls = std::move(calls);
        m.content = std::move(content_);
        return m;
    }

    Json to_value() const {
        Json obj = Json::object();
        obj["role"] = role;
        if (parts.empty()) {
            obj["content"] = content;
        } else {
            Json arr;
            wire::encode(arr, parts);
            obj["content"] = std::move(arr);
        }
        if (!tool_calls.empty()) {
            Json arr;
            wire::encode(arr, tool_calls);
            obj["tool_calls"] = std::move(arr);
        }
        if (tool_call_id) obj["tool_call_id"] = tool_call_id.get();
        if (name) obj["name"] = name.get();
        return obj;
    }
    bool from_value(const Json& v) {
        if (!v.is_object()) return false;
        if (const auto r = v.find("role"); r != v.end() && r->is_string())
            role = r->get<std::string>();
        content.clear();
        parts.clear();
        tool_calls.clear();
        tool_call_id.clear();
        name.clear();
        if (const auto t = v.find("tool_calls"); t != v.end() && t->is_array())
            if (!wire::decode(*t, tool_calls)) return false;
        if (const auto t = v.find("tool_call_id"); t != v.end() && t->is_string())
            tool_call_id = t->get<std::string>();
        if (const auto t = v.find("name"); t != v.end() && t->is_string())
            name = t->get<std::string>();
        const auto c = v.find("content");
        if (c == v.end() || c->is_null()) return true;
        if (c->is_string()) { content = c->get<std::string>(); return true; }
        return wire::decode(*c, parts);
    }
};

struct ChatRequest : Model<ChatRequest> {
    std::string              model;
    std::vector<ChatMessage> messages;
    bool                     stream{false};
    JsonOptional<f64>        temperature;
    JsonOptional<f64>        top_p;
    JsonOptional<int>        top_k;
    JsonOptional<f64>        repeat_penalty;
    JsonOptional<int>        max_completion_tokens;
    JsonOptional<std::vector<std::string>> stop;
    JsonOptional<bool>       enable_thinking;
    JsonOptional<Json>       chat_template_kwargs;
    JsonOptional<std::vector<ToolDefinition>> tools;
    JsonOptional<Json>       tool_choice;
    template <class V> void fields(V&& v) {
        v("model", model);
        v("messages", messages);
        v("stream", stream);
        v("temperature", temperature);
        v("top_p", top_p);
        v("top_k", top_k);
        v("repeat_penalty", repeat_penalty);
        v("max_completion_tokens", max_completion_tokens);
        v("stop", stop);
        v("enable_thinking", enable_thinking);
        v("chat_template_kwargs", chat_template_kwargs);
        v("tools", tools);
        v("tool_choice", tool_choice);
    }
};

struct ChatChoice : Model<ChatChoice> {
    int                       index{0};
    ChatMessage               message;
    JsonOptional<std::string> finish_reason;
    template <class V> void fields(V&& v) {
        v("index", index);
        v("message", message);
        v("finish_reason", finish_reason);
    }
};

struct Usage : Model<Usage> {
    JsonOptional<i64> prompt_tokens;
    JsonOptional<i64> completion_tokens;
    JsonOptional<i64> total_tokens;
    template <class V> void fields(V&& v) {
        v("prompt_tokens", prompt_tokens);
        v("completion_tokens", completion_tokens);
        v("total_tokens", total_tokens);
    }
};

struct ChatResponse : Model<ChatResponse> {
    std::string             id;
    std::string             model;
    std::vector<ChatChoice> choices;
    JsonOptional<Usage>     usage;
    template <class V> void fields(V&& v) {
        v("id", id);
        v("model", model);
        v("choices", choices);
        v("usage", usage);
    }
    const std::string* text() const {
        if (choices.empty()) return nullptr;
        return &choices.front().message.content;
    }
    const std::vector<ToolCall>* calls() const {
        if (choices.empty() || choices.front().message.tool_calls.empty()) return nullptr;
        return &choices.front().message.tool_calls;
    }
};

struct ChatDeltaContent : Model<ChatDeltaContent> {
    JsonOptional<std::string> content;
    JsonOptional<std::string> reasoning_content;
    JsonOptional<std::string> reasoning;
    JsonOptional<std::string> role;
    JsonOptional<std::vector<ToolCallDelta>> tool_calls;
    template <class V> void fields(V&& v) {
        v("content", content);
        v("reasoning_content", reasoning_content);
        v("reasoning", reasoning);
        v("role", role);
        v("tool_calls", tool_calls);
    }
};

struct ChatDeltaChoice : Model<ChatDeltaChoice> {
    ChatDeltaContent          delta;
    JsonOptional<std::string> finish_reason;
    template <class V> void fields(V&& v) {
        v("delta", delta);
        v("finish_reason", finish_reason);
    }
};

struct ChatDelta : Model<ChatDelta> {
    std::vector<ChatDeltaChoice> choices;
    template <class V> void fields(V&& v) { v("choices", choices); }
};

}
