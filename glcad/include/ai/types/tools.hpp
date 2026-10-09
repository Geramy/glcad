#pragma once

#include "ai/model.hpp"

namespace gai {

struct ToolFunction : Model<ToolFunction> {
    std::string name;
    std::string description;
    Json        parameters = Json::object();
    template <class V> void fields(V&& v) {
        v("name", name);
        v("description", description);
        v("parameters", parameters);
    }
};

struct ToolDefinition : Model<ToolDefinition> {
    std::string  type{"function"};
    ToolFunction function;
    template <class V> void fields(V&& v) {
        v("type", type);
        v("function", function);
    }
    static ToolDefinition make(std::string name, std::string description, Json parameters) {
        ToolDefinition d;
        d.function.name = std::move(name);
        d.function.description = std::move(description);
        d.function.parameters = std::move(parameters);
        return d;
    }
};

struct ToolCallFunction : Model<ToolCallFunction> {
    JsonOptional<std::string> name;
    JsonOptional<std::string> arguments;
    template <class V> void fields(V&& v) {
        v("name", name);
        v("arguments", arguments);
    }
};

struct ToolCall : Model<ToolCall> {
    JsonOptional<std::string> id;
    std::string               type{"function"};
    ToolCallFunction          function;
    template <class V> void fields(V&& v) {
        v("id", id);
        v("type", type);
        v("function", function);
    }
    const std::string& name() const noexcept {
        static const std::string empty;
        return function.name ? function.name.get() : empty;
    }
    const std::string& arguments() const noexcept {
        static const std::string empty;
        return function.arguments ? function.arguments.get() : empty;
    }
};

struct ToolCallDelta : Model<ToolCallDelta> {
    int                            index{0};
    JsonOptional<std::string>      id;
    JsonOptional<ToolCallFunction> function;
    template <class V> void fields(V&& v) {
        v("index", index);
        v("id", id);
        v("function", function);
    }
};

}
