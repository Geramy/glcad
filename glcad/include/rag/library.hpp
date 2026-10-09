#pragma once

#include <variant>

#include "rag/sidecar_store.hpp"
#ifdef GLCAD_WITH_PGLITE
#include "rag/pglite_store.hpp"
#endif

namespace grag {

enum class Backend : gcm::u8 { Sidecar, Postgres };

class Library {
public:
    explicit Library(std::string dir, Backend want = Backend::Sidecar)
        : store_(make(std::move(dir), want)) {}

    Backend backend() const noexcept {
#ifdef GLCAD_WITH_PGLITE
        if (std::holds_alternative<PgliteStore>(store_)) return Backend::Postgres;
#endif
        return Backend::Sidecar;
    }

    std::string save(const std::string& title, const std::string& text,
                     const std::string& source) {
        return std::visit([&](auto& s) { return s.save(title, text, source); }, store_);
    }
    bool ensure_indexed(const std::string& model_id, const Embedder& embed) {
        return std::visit([&](auto& s) { return s.ensure_indexed(model_id, embed); },
                          store_);
    }
    std::vector<Hit> lookup(const std::string& query, int k,
                            const std::string& model_id, const Embedder& embed_query,
                            bool* used_embeddings = nullptr) {
        return std::visit(
            [&](auto& s) {
                return s.lookup(query, k, model_id, embed_query, used_embeddings);
            },
            store_);
    }
    bool empty() {
        return std::visit([](auto& s) { return s.empty(); }, store_);
    }
    const std::string& dir() const {
        return std::visit([](const auto& s) -> const std::string& { return s.dir(); },
                          store_);
    }

private:
#ifdef GLCAD_WITH_PGLITE
    using StoreVariant = std::variant<SidecarStore, PgliteStore>;
#else
    using StoreVariant = std::variant<SidecarStore>;
#endif

    static StoreVariant make(std::string dir, Backend want) {
#ifdef GLCAD_WITH_PGLITE
        if (want == Backend::Postgres && PgliteStore(dir).ready())
            return StoreVariant(std::in_place_type<PgliteStore>, std::move(dir));
#else
        (void)want;
#endif
        return StoreVariant(std::in_place_type<SidecarStore>, std::move(dir));
    }

    StoreVariant store_;
};

}
