#pragma once

#include <mutex>

#include "rag/store.hpp"

namespace grag {

class SidecarStore : public Store<SidecarStore> {
public:
    explicit SidecarStore(std::string dir);

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
    struct Chunk {
        std::string file;
        std::string text;
        std::vector<float> vec;
    };
    std::vector<Chunk>& chunks(const std::string& model_id);
    std::string index_dir() const;
    bool read_sidecar(const std::string& source_name, const std::string& model,
                      gcm::u64 mtime, std::vector<Chunk>& out) const;
    void write_sidecar(const std::string& source_name, const std::string& model,
                       gcm::u64 mtime, const std::vector<const Chunk*>& list) const;
    bool ensure_indexed_locked(const std::string& model_id, const Embedder& embed);

    std::string dir_;
    std::mutex  mu_;
    std::vector<Chunk> cache_;
    std::string cache_model_;
    std::vector<std::pair<std::string, gcm::u64>> cache_files_;
};

}
