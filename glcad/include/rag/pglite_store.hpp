#pragma once

#include <mutex>

#include "rag/store.hpp"

namespace grag {

class PgliteStore : public Store<PgliteStore> {
public:
    explicit PgliteStore(std::string dir);

    bool ready();

    std::string save_impl(const std::string& title, const std::string& text,
                          const std::string& source);
    bool ensure_indexed_impl(const std::string& model_id, const Embedder& embed);
    std::vector<Hit> lookup_impl(const std::string& query, int k,
                                 const std::string& model_id,
                                 const Embedder& embed_query,
                                 bool* used_embeddings);
    bool empty_impl();
    const std::string& dir_impl() const noexcept { return dir_; }

private:
    bool open_locked();
    bool sync_files_locked();
    bool ensure_indexed_locked(const std::string& model_id, const Embedder& embed);

    std::string dir_;
    std::mutex  mu_;
    bool        schema_ok_ = false;
};

}
