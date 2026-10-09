#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "math/config.hpp"

namespace grag {

struct Hit {
    std::string file;
    std::string text;
};

using Embedder =
    std::function<std::vector<std::vector<float>>(const std::vector<std::string>&)>;

template <class D>
class Store {
public:
    std::string save(const std::string& title, const std::string& text,
                     const std::string& source) {
        return self().save_impl(title, text, source);
    }
    bool ensure_indexed(const std::string& model_id, const Embedder& embed) {
        return self().ensure_indexed_impl(model_id, embed);
    }
    std::vector<Hit> lookup(const std::string& query, int k,
                            const std::string& model_id, const Embedder& embed_query,
                            bool* used_embeddings = nullptr) {
        return self().lookup_impl(query, k, model_id, embed_query, used_embeddings);
    }
    bool empty() { return self().empty_impl(); }
    const std::string& dir() const { return self().dir_impl(); }

protected:
    static std::string slug_from(const std::string& title) {
        std::string slug;
        for (char c : title) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                slug.push_back(char(std::tolower(static_cast<unsigned char>(c))));
            else if (!slug.empty() && slug.back() != '-')
                slug.push_back('-');
        }
        while (!slug.empty() && slug.back() == '-') slug.pop_back();
        if (slug.empty()) slug = "standard";
        if (slug.size() > 80) slug.resize(80);
        return slug;
    }

    static std::vector<std::string> chunk_text(const std::string& text) {
        std::vector<std::string> out;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t end = std::min(at + 1200, text.size());
            if (end < text.size()) {
                const std::size_t para = text.rfind("\n\n", end);
                if (para != std::string::npos && para > at + 400) end = para;
            }
            std::string piece = text.substr(at, end - at);
            if (!piece.empty()) out.push_back(std::move(piece));
            at = end;
        }
        return out;
    }

    static float cosine_scored(const std::vector<float>& a,
                               const std::vector<float>& b) {
        if (a.size() != b.size() || a.empty()) return -1.0f;
        float dot = 0, na = 0, nb = 0;
        for (std::size_t d = 0; d < a.size(); ++d) {
            dot += a[d] * b[d];
            na += a[d] * a[d];
            nb += b[d] * b[d];
        }
        const float cos = (na > 0 && nb > 0) ? dot / std::sqrt(na * nb) : 0.0f;
        return cos > 0.25f ? cos : -1.0f;
    }

    static float term_score(const std::string& text_lower,
                            const std::vector<std::string>& query_tokens) {
        float score = 0;
        for (const auto& t : query_tokens) {
            if (t.size() < 4) continue;
            std::size_t at = 0, hits = 0;
            while ((at = text_lower.find(t, at)) != std::string::npos) {
                ++hits;
                at += t.size();
            }
            score += float(hits) * float(t.size());
        }
        return score;
    }

    static std::vector<std::string> tokens_of(const std::string& s) {
        std::vector<std::string> out;
        std::string word;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                word.push_back(char(std::tolower(static_cast<unsigned char>(c))));
            else if (!word.empty()) {
                out.push_back(std::move(word));
                word.clear();
            }
        }
        if (!word.empty()) out.push_back(std::move(word));
        return out;
    }

private:
    D&       self() noexcept { return *static_cast<D*>(this); }
    const D& self() const noexcept { return *static_cast<const D*>(this); }
};

}
