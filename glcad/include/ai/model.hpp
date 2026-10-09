#pragma once

#include <string>
#include <type_traits>
#include <vector>

#include "ai/json.hpp"

namespace gai {

using gcm::f64;
using gcm::i64;

template <class D> struct Model;

// Three states: absent, present-and-null, present-with-value.
template <class T>
class JsonOptional {
public:
    JsonOptional() = default;
    JsonOptional(T v) : value_(std::move(v)), set_(true) {}
    JsonOptional& operator=(T v) { value_ = std::move(v); set_ = true; null_ = false; return *this; }

    bool exists() const noexcept { return set_; }
    bool is_null() const noexcept { return set_ && null_; }
    bool has() const noexcept { return set_ && !null_; }
    explicit operator bool() const noexcept { return has(); }

    const T& get() const noexcept { return value_; }
    T&       emplace() { set_ = true; null_ = false; return value_; }
    void     set_null() noexcept { set_ = true; null_ = true; value_ = T{}; }
    void     clear() noexcept { set_ = false; null_ = false; value_ = T{}; }

private:
    T    value_{};
    bool set_{false};
    bool null_{false};
};

namespace wire {

inline void encode(Json& out, const bool& v)        { out = v; }
inline void encode(Json& out, const int& v)         { out = v; }
inline void encode(Json& out, const i64& v)         { out = v; }
inline void encode(Json& out, const f64& v)         { out = v; }
inline void encode(Json& out, const std::string& v) { out = v; }

inline bool decode(const Json& in, bool& v)        { if (!in.is_boolean()) return false; v = in.get<bool>(); return true; }
inline bool decode(const Json& in, int& v)         { if (!in.is_number()) return false; v = in.get<int>(); return true; }
inline bool decode(const Json& in, i64& v)         { if (!in.is_number()) return false; v = in.get<i64>(); return true; }
inline bool decode(const Json& in, f64& v)         { if (!in.is_number()) return false; v = in.get<f64>(); return true; }
inline bool decode(const Json& in, std::string& v) { if (!in.is_string()) return false; v = in.get<std::string>(); return true; }

inline void encode(Json& out, const Json& v) { out = v; }
inline bool decode(const Json& in, Json& v)  { v = in; return true; }

template <class T, class = std::enable_if_t<std::is_base_of_v<Model<T>, T>>>
void encode(Json& out, const T& v) { out = v.to_value(); }
template <class T, class = std::enable_if_t<std::is_base_of_v<Model<T>, T>>>
bool decode(const Json& in, T& v) { return v.from_value(in); }

template <class T>
void encode(Json& out, const std::vector<T>& v) {
    out = Json::array();
    for (const auto& e : v) {
        Json item;
        encode(item, e);
        out.push_back(std::move(item));
    }
}
template <class T>
bool decode(const Json& in, std::vector<T>& v) {
    if (!in.is_array()) return false;
    v.clear();
    v.reserve(in.size());
    for (const auto& item : in) {
        T e{};
        if (!decode(item, e)) return false;
        v.push_back(std::move(e));
    }
    return true;
}

struct Writer {
    Json& obj;
    template <class T>
    void operator()(const char* key, const T& field) {
        Json v;
        encode(v, field);
        obj[key] = std::move(v);
    }
    template <class T>
    void operator()(const char* key, const JsonOptional<T>& field) {
        if (!field.exists()) return;
        if (field.is_null()) { obj[key] = nullptr; return; }
        Json v;
        encode(v, field.get());
        obj[key] = std::move(v);
    }
};

struct Reader {
    const Json& obj;
    bool ok{true};
    template <class T>
    void operator()(const char* key, T& field) {
        const auto it = obj.find(key);
        if (it == obj.end() || it->is_null()) return;
        if (!decode(*it, field)) ok = false;
    }
    template <class T>
    void operator()(const char* key, JsonOptional<T>& field) {
        const auto it = obj.find(key);
        if (it == obj.end()) { field.clear(); return; }
        if (it->is_null()) { field.set_null(); return; }
        if (!decode(*it, field.emplace())) ok = false;
    }
};

}

template <class D>
struct Model {
    Json to_value() const {
        Json obj = Json::object();
        wire::Writer w{obj};
        const_cast<D&>(static_cast<const D&>(*this)).fields(w);
        return obj;
    }
    bool from_value(const Json& v) {
        if (!v.is_object()) return false;
        wire::Reader r{v};
        static_cast<D&>(*this).fields(r);
        return r.ok;
    }
    std::string to_json() const { return static_cast<const D&>(*this).to_value().dump(); }
    bool parse(std::string_view text) {
        const auto v = parse_json(text);
        return v && static_cast<D&>(*this).from_value(*v);
    }
};

}
