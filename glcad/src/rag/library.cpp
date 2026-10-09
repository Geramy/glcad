#include "rag/sidecar_store.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace grag {

namespace fs = std::filesystem;

namespace {

// "GLRAG1" | model_len,model | mtime u64 | count u32 | {len,text,dim,floats}
constexpr char kMagic[] = "GLRAG1";

template <class T> void put_pod(std::ofstream& f, T v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
template <class T> bool get_pod(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(v));
    return bool(f);
}

gcm::u64 mtime_key(const fs::file_time_type& t) {
    return gcm::u64(t.time_since_epoch().count());
}

}

SidecarStore::SidecarStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    fs::create_directories(fs::path(dir_) / ".index", ec);
}

std::string SidecarStore::index_dir() const {
    return (fs::path(dir_) / ".index").string();
}

bool SidecarStore::empty_impl() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec))
        if (e.is_regular_file()) return false;
    return true;
}

std::string SidecarStore::save_impl(const std::string& title, const std::string& text,
                                    const std::string& source) {
    if (title.empty() || text.empty()) return {};
    const std::string path =
        (fs::path(dir_) / (slug_from(title) + ".md")).string();
    std::ofstream f(path);
    if (!f) return {};
    f << "# " << title << "\n\n";
    if (!source.empty()) f << "Source: " << source << "\n\n";
    f << text.substr(0, 200000) << "\n";
    return path;
}

bool SidecarStore::read_sidecar(const std::string& source_name,
                                const std::string& model, gcm::u64 mtime,
                                std::vector<Chunk>& out) const {
    std::ifstream f((fs::path(index_dir()) / (source_name + ".vec")).string(),
                    std::ios::binary);
    if (!f) return false;
    char magic[6];
    f.read(magic, 6);
    if (!f || std::memcmp(magic, kMagic, 6) != 0) return false;
    gcm::u32 mlen = 0;
    if (!get_pod(f, mlen) || mlen > 256) return false;
    std::string m(mlen, '\0');
    f.read(m.data(), mlen);
    gcm::u64 mt = 0;
    gcm::u32 count = 0;
    if (!get_pod(f, mt) || !get_pod(f, count) || count > 100000) return false;
    if (m != model || mt != mtime) return false;
    std::vector<Chunk> got;
    got.reserve(count);
    for (gcm::u32 i = 0; i < count; ++i) {
        gcm::u32 tlen = 0;
        if (!get_pod(f, tlen) || tlen > (1u << 20)) return false;
        Chunk c;
        c.file = source_name;
        c.text.resize(tlen);
        f.read(c.text.data(), tlen);
        gcm::u32 dim = 0;
        if (!get_pod(f, dim) || dim > 65536) return false;
        c.vec.resize(dim);
        f.read(reinterpret_cast<char*>(c.vec.data()), dim * sizeof(float));
        if (!f) return false;
        got.push_back(std::move(c));
    }
    out.insert(out.end(), got.begin(), got.end());
    return true;
}

void SidecarStore::write_sidecar(const std::string& source_name,
                                 const std::string& model, gcm::u64 mtime,
                                 const std::vector<const Chunk*>& list) const {
    const std::string path =
        (fs::path(index_dir()) / (source_name + ".vec")).string();
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return;
        f.write(kMagic, 6);
        put_pod(f, gcm::u32(model.size()));
        f.write(model.data(), std::streamsize(model.size()));
        put_pod(f, mtime);
        put_pod(f, gcm::u32(list.size()));
        for (const Chunk* c : list) {
            put_pod(f, gcm::u32(c->text.size()));
            f.write(c->text.data(), std::streamsize(c->text.size()));
            put_pod(f, gcm::u32(c->vec.size()));
            f.write(reinterpret_cast<const char*>(c->vec.data()),
                    std::streamsize(c->vec.size() * sizeof(float)));
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
}

std::vector<SidecarStore::Chunk>& SidecarStore::chunks(const std::string& model_id) {
    std::vector<std::pair<std::string, gcm::u64>> now;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".txt" && ext != ".md" && ext != ".csv") continue;
        now.emplace_back(e.path().string(),
                         mtime_key(fs::last_write_time(e.path(), ec)));
    }
    std::sort(now.begin(), now.end());
    if (now == cache_files_ && model_id == cache_model_) return cache_;

    cache_files_ = now;
    cache_model_ = model_id;
    cache_.clear();
    for (const auto& [path, mt] : now) {
        const std::string name = fs::path(path).filename().string();
        if (!model_id.empty() && read_sidecar(name, model_id, mt, cache_)) continue;
        std::ifstream f(path);
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        for (auto& piece : chunk_text(text))
            cache_.push_back({name, std::move(piece), {}});
    }
    return cache_;
}

bool SidecarStore::ensure_indexed_locked(const std::string& model_id,
                                         const Embedder& embed) {
    if (model_id.empty() || !embed) return false;
    auto& all = chunks(model_id);
    std::vector<std::size_t> todo;
    std::vector<std::string> texts;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].vec.empty()) {
            todo.push_back(i);
            texts.push_back(all[i].text);
        }
    if (!todo.empty()) {
        const auto vecs = embed(texts);
        if (vecs.size() != todo.size()) return false;
        for (std::size_t j = 0; j < todo.size(); ++j) all[todo[j]].vec = vecs[j];
        std::map<std::string, std::vector<const Chunk*>> by_file;
        for (const auto& c : all) by_file[c.file].push_back(&c);
        std::error_code ec;
        for (const auto& [name, list] : by_file) {
            bool complete = true;
            for (const Chunk* c : list) complete = complete && !c->vec.empty();
            if (!complete) continue;
            const auto mt = fs::last_write_time(fs::path(dir_) / name, ec);
            if (!ec) write_sidecar(name, model_id, mtime_key(mt), list);
        }
    }
    return true;
}

bool SidecarStore::ensure_indexed_impl(const std::string& model_id,
                                       const Embedder& embed) {
    std::lock_guard<std::mutex> lock(mu_);
    return ensure_indexed_locked(model_id, embed);
}

std::vector<Hit> SidecarStore::lookup_impl(const std::string& query, int k,
                                           const std::string& model_id,
                                           const Embedder& embed_query,
                                           bool* used_embeddings) {
    if (used_embeddings) *used_embeddings = false;
    if (query.empty()) return {};
    k = std::clamp(k, 1, 10);

    std::lock_guard<std::mutex> lock(mu_);
    const bool indexed = ensure_indexed_locked(model_id, embed_query);
    auto& all = chunks(model_id);
    if (all.empty()) return {};

    std::vector<std::pair<float, std::size_t>> scored;
    bool embedded = false;
    if (indexed) {
        const auto qv_all = embed_query({query});
        if (!qv_all.empty() && !qv_all[0].empty()) {
            for (std::size_t i = 0; i < all.size(); ++i) {
                const float cos = cosine_scored(all[i].vec, qv_all[0]);
                if (cos >= 0.0f) scored.push_back({cos, i});
            }
            embedded = true;
        }
    }
    if (!embedded) {
        const auto q = tokens_of(query);
        for (std::size_t i = 0; i < all.size(); ++i) {
            std::string low = all[i].text;
            for (char& c : low)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            const float score = term_score(low, q);
            if (score > 0) scored.push_back({score, i});
        }
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    if (used_embeddings) *used_embeddings = embedded;
    std::vector<Hit> out;
    for (int i = 0; i < k && i < int(scored.size()); ++i) {
        const auto& c = all[scored[std::size_t(i)].second];
        out.push_back({c.file, c.text});
    }
    return out;
}

}
