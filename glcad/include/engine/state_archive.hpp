#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace gfile::state {
template<class T> struct Fields;
template<class T> struct IsVector : std::false_type {};
template<class T> struct IsVector<std::vector<T>> : std::true_type {};
// Sorted so identical maps serialise to identical bytes.
template<class T> struct IsStringMap : std::false_type {};
template<class V> struct IsStringMap<std::unordered_map<std::string, V>> : std::true_type {};

struct Writer {
    std::vector<std::uint8_t> bytes;
    void word(std::uint64_t n) {
        for (int i = 0; i < 8; ++i) bytes.push_back(std::uint8_t(n >> (i * 8)));
    }
    template<class... T> void operator()(const T&... value) { (put(value), ...); }
    template<class T> void put(const T& v) {
        if constexpr (std::is_same_v<T, bool>) bytes.push_back(v ? 1 : 0);
        else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) word(static_cast<std::uint64_t>(v));
        else if constexpr (std::is_same_v<T, double>) word(std::bit_cast<std::uint64_t>(v));
        else if constexpr (std::is_same_v<T, float>) word(std::bit_cast<std::uint32_t>(v));
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<std::uint8_t>>) {
            word(v.size()); bytes.insert(bytes.end(), v.begin(), v.end());
        } else if constexpr (IsVector<T>::value) {
            word(v.size()); for (const auto& x : v) put(x);
        } else if constexpr (IsStringMap<T>::value) {
            std::vector<const typename T::value_type*> sorted;
            sorted.reserve(v.size());
            for (const auto& kv : v) sorted.push_back(&kv);
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto* a, const auto* b) { return a->first < b->first; });
            word(sorted.size());
            for (const auto* kv : sorted) { put(kv->first); put(kv->second); }
        } else Fields<T>::visit(*this, v);
    }
};

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t at{0};
    std::size_t allocation_left{256u << 20};
    void allocate(std::uint64_t count, std::size_t size) {
        if (count > allocation_left / size) throw std::runtime_error("document state allocation limit");
        allocation_left -= static_cast<std::size_t>(count) * size;
    }
    void need(std::size_t n) const {
        if (n > bytes.size() - at) throw std::runtime_error("truncated document state");
    }
    std::uint64_t word() {
        need(8); std::uint64_t n = 0;
        for (int i = 0; i < 8; ++i) n |= std::uint64_t(bytes[at++]) << (i * 8);
        return n;
    }
    template<class... T> void operator()(T&... value) { (get(value), ...); }
    template<class T> void get(T& v) {
        if constexpr (std::is_same_v<T, bool>) {
            need(1); const auto b = bytes[at++];
            if (b > 1) throw std::runtime_error("invalid boolean in document state");
            v = b != 0;
        } else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) v = static_cast<T>(word());
        else if constexpr (std::is_same_v<T, double>) v = std::bit_cast<double>(word());
        else if constexpr (std::is_same_v<T, float>) v = std::bit_cast<float>(std::uint32_t(word()));
        else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::vector<std::uint8_t>>) {
            const auto n = word(); need(n); allocate(n, 1);
            v.assign(bytes.begin() + at, bytes.begin() + at + n); at += n;
        } else if constexpr (IsVector<T>::value) {
            const auto n = word(); need(n);
            if (n > (1u << 24)) throw std::runtime_error("implausible document state count");
            allocate(n, sizeof(typename T::value_type));
            v.resize(n); for (auto& x : v) get(x);
        } else if constexpr (IsStringMap<T>::value) {
            const auto n = word(); need(n);
            if (n > (1u << 24)) throw std::runtime_error("implausible document state count");
            allocate(n, sizeof(typename T::value_type));
            v.clear(); v.reserve(n);
            for (std::uint64_t i = 0; i < n; ++i) {
                std::string key; typename T::mapped_type value;
                get(key); get(value);
                v.emplace(std::move(key), std::move(value));
            }
        } else Fields<T>::visit(*this, v);
    }
    void finish() const {
        if (at != bytes.size()) throw std::runtime_error("unexpected document state data");
    }
};
}
