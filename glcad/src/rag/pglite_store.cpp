#include "rag/pglite_store.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <pthread.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

extern "C" {
#include "pglite_native.h"
}

namespace grag {

namespace fs = std::filesystem;

namespace {

// The backend is single-threaded: every engine call runs on one thread.

std::vector<std::uint8_t> g_in;
std::size_t               g_in_off = 0;
std::vector<std::uint8_t> g_out;

#ifdef _WIN32
// The mingw CRT has no setenv; the boot code below speaks POSIX.
int setenv(const char* name, const char* value, int) {
    return _putenv_s(name, value);
}
#endif

ssize_t engine_read(void* buf, std::size_t max) {
    std::size_t n = g_in.size() - g_in_off;
    if (n > max) n = max;
    std::memcpy(buf, g_in.data() + g_in_off, n);
    g_in_off += n;
    return ssize_t(n);
}

ssize_t engine_write(void* buf, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    g_out.insert(g_out.end(), p, p + len);
    return ssize_t(len);
}

void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(std::uint8_t(x >> 24));
    v.push_back(std::uint8_t(x >> 16));
    v.push_back(std::uint8_t(x >> 8));
    v.push_back(std::uint8_t(x));
}

// Single-quoted for sh; cmd.exe has no single quotes, so double-quoted there.
std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s)
        if (c != '"') out.push_back(c);
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
#endif
}

class Engine {
public:
    static Engine& the() {
        static Engine e;
        return e;
    }

    bool open(const std::string& runtime, const std::string& pgdata) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!bound_.empty()) return bound_ == pgdata && booted_;
        bound_ = pgdata;
        run([&] { booted_ = boot(runtime, pgdata); });
        return booted_;
    }

    std::vector<std::uint8_t> exec(std::vector<std::uint8_t> wire) {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::uint8_t> reply;
        if (!booted_) return reply;
        run([&] {
            g_in = std::move(wire);
            g_in_off = 0;
            g_out.clear();
            pump_until_drained();
            reply = std::move(g_out);
        });
        return reply;
    }

private:
    Engine() {
        // The default thread stack (512 KB on macOS) is too shallow for the backend.
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
        pthread_create(&worker_, &attr,
                       [](void* self) -> void* {
                           static_cast<Engine*>(self)->serve();
                           return nullptr;
                       },
                       this);
        pthread_attr_destroy(&attr);
    }

    ~Engine() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            // Data directory already gone: the shutdown checkpoint would PANIC.
            if (booted_ && !fs::exists(bound_)) booted_ = false;
            if (booted_) {
                run([&] {
                    g_in = {'X', 0, 0, 0, 4};
                    g_in_off = 0;
                    g_out.clear();
                    pump_until_drained();
                    pgl_setPGliteActive(0);
                    pgl_run_atexit_funcs();
                });
                booted_ = false;
            }
        }
        {
            std::lock_guard<std::mutex> lk(qm_);
            stop_ = true;
        }
        qcv_.notify_all();
        pthread_join(worker_, nullptr);
    }

    void run(std::function<void()> job) {
        std::unique_lock<std::mutex> lk(qm_);
        job_ = std::move(job);
        qcv_.notify_all();
        qcv_.wait(lk, [&] { return !job_; });
    }

    void serve() {
        std::unique_lock<std::mutex> lk(qm_);
        for (;;) {
            qcv_.wait(lk, [&] { return job_ || stop_; });
            if (job_) {
                job_();
                job_ = nullptr;
                qcv_.notify_all();
            }
            if (stop_ && !job_) return;
        }
    }

    static void pump_until_drained() {
        while (g_in_off < g_in.size() || pq_buffer_remaining_data() > 0) {
            const int rc = pgl_native_pump();
            if (rc == 100) PostgresMainLongJmp();
        }
        PostgresSendReadyForQueryIfNecessary();
        pgl_pq_flush();
    }

    static bool boot(const std::string& runtime, const std::string& pgdata) {
        setenv("PGUSER", "postgres", 1);
        setenv("PGDATABASE", "postgres", 1);
        setenv("TZ", "UTC", 1);
        setenv("PGTZ", "UTC", 1);
        setenv("PGCLIENTENCODING", "UTF8", 1);
        setenv("LANG", "C", 1);

        if (!fs::exists(fs::path(pgdata) / "PG_VERSION")) {
            // Copy the build-time initdb'd template: the only way to initdb on iOS.
            const fs::path tpl = fs::path(runtime) / "pgdata-template";
            std::error_code ec;
            if (fs::exists(tpl / "PG_VERSION")) {
                fs::copy(tpl, pgdata, fs::copy_options::recursive, ec);
                if (ec) return false;
                // 0700 dirs and 0600 files, as the backend's permission check requires.
                fs::permissions(pgdata, fs::perms::owner_all,
                                fs::perm_options::replace, ec);
                for (auto it = fs::recursive_directory_iterator(pgdata, ec);
                     !ec && it != fs::recursive_directory_iterator();
                     it.increment(ec)) {
                    std::error_code pec;
                    fs::permissions(it->path(),
                                    it->is_directory(pec)
                                        ? fs::perms::owner_all
                                        : (fs::perms::owner_read |
                                           fs::perms::owner_write),
                                    fs::perm_options::replace, pec);
                }
            } else {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
                // No subprocesses on iOS: without a template there is no initdb.
                return false;
#elif defined(_WIN32)
                // cmd.exe strips the outer quotes, so wrap the quoted command once more.
                const std::string cmd = "\"" +
                    shell_quote(runtime + "/bin/initdb.exe") +
                    " --allow-group-access --encoding UTF8 --locale=C"
                    " --locale-provider=libc --auth=trust --no-sync -U postgres -D " +
                    shell_quote(pgdata) + " >NUL 2>&1\"";
                if (std::system(cmd.c_str()) != 0) return false;
#else
                const std::string cmd =
                    shell_quote(runtime + "/bin/initdb") +
                    " --allow-group-access --encoding UTF8 --locale=C"
                    " --locale-provider=libc --auth=trust --no-sync -U postgres -D " +
                    shell_quote(pgdata) + " >/dev/null 2>&1";
                if (std::system(cmd.c_str()) != 0) return false;
#endif
            }
        }

        pgl_native_setup();
#ifdef _WIN32
        pgl_freopen("NUL", "r", 0);
#else
        pgl_freopen("/dev/null", "r", 0);
#endif
        pgl_set_rw_cbs(engine_read, engine_write);
        pgl_setPGliteActive(1);

        // ps_status clobbers argv as one contiguous span; static: save_argv keeps it.
        const std::vector<std::string> args = {
            runtime + "/bin/postgres", "--single", "-F", "-O", "-j",
            "-c", "search_path=public",
            "-c", "exit_on_error=false",
            "-c", "log_checkpoints=false",
            "-c", "max_worker_processes=0",
            "-c", "max_parallel_workers=0",
            "-c", "max_parallel_workers_per_gather=0",
            "-c", "max_parallel_maintenance_workers=0",
            "-D", pgdata, "postgres"};
        static std::vector<char> block;
        static std::vector<char*> argv;
        std::size_t total = 0;
        for (const auto& a : args) total += a.size() + 1;
        block.assign(total, '\0');
        argv.clear();
        char* at = block.data();
        for (const auto& a : args) {
            std::memcpy(at, a.c_str(), a.size() + 1);
            argv.push_back(at);
            at += a.size() + 1;
        }
        argv.push_back(nullptr);
        if (pgl_native_call(pgl_backend_main, int(args.size()), argv.data()) != 99)
            return false;

        pgl_startPGlite();

        std::string params;
        for (const char* kv : {"user", "postgres", "database", "postgres",
                               "client_encoding", "UTF8"}) {
            params += kv;
            params.push_back('\0');
        }
        params.push_back('\0');
        std::vector<std::uint8_t> pkt;
        put_u32(pkt, std::uint32_t(4 + 4 + params.size()));
        put_u32(pkt, 196608);          // protocol 3.0
        pkt.insert(pkt.end(), params.begin(), params.end());
        g_in = std::move(pkt);
        g_in_off = 0;
        g_out.clear();
        if (ProcessStartupPacket(pgl_getMyProcPort(), true, true) != 0) return false;
        pgl_sendConnData();
        pgl_pq_flush();
        g_out.clear();
        return true;
    }

    std::mutex  mu_;
    std::string bound_;
    bool        booted_ = false;

    pthread_t               worker_{};
    std::mutex              qm_;
    std::condition_variable qcv_;
    std::function<void()>   job_;
    bool                    stop_ = false;
};

struct QResult {
    bool ok = true;
    std::string error;
    std::vector<std::vector<std::string>> rows;
};

QResult run_sql(const std::string& sql) {
    std::vector<std::uint8_t> wire;
    wire.push_back('Q');
    put_u32(wire, std::uint32_t(4 + sql.size() + 1));
    wire.insert(wire.end(), sql.begin(), sql.end());
    wire.push_back(0);
    const auto reply = Engine::the().exec(std::move(wire));

    QResult r;
    if (reply.empty()) {
        r.ok = false;
        r.error = "engine unavailable";
        return r;
    }
    std::size_t off = 0;
    while (off + 5 <= reply.size()) {
        const char type = char(reply[off]);
        const std::uint32_t len = (std::uint32_t(reply[off + 1]) << 24) |
                                  (std::uint32_t(reply[off + 2]) << 16) |
                                  (std::uint32_t(reply[off + 3]) << 8) |
                                  reply[off + 4];
        if (len < 4 || off + 1 + len > reply.size()) break;
        const std::uint8_t* body = reply.data() + off + 5;
        const std::size_t blen = len - 4;
        if (type == 'D' && blen >= 2) {
            const std::uint16_t nf = std::uint16_t((body[0] << 8) | body[1]);
            std::size_t p = 2;
            std::vector<std::string> row;
            for (std::uint16_t f = 0; f < nf && p + 4 <= blen; ++f) {
                const auto flen = std::int32_t((std::uint32_t(body[p]) << 24) |
                                               (std::uint32_t(body[p + 1]) << 16) |
                                               (std::uint32_t(body[p + 2]) << 8) |
                                               body[p + 3]);
                p += 4;
                if (flen < 0) {
                    row.emplace_back();
                } else {
                    row.emplace_back(reinterpret_cast<const char*>(body + p),
                                     std::size_t(flen));
                    p += std::size_t(flen);
                }
            }
            r.rows.push_back(std::move(row));
        } else if (type == 'E') {
            r.ok = false;
            std::size_t p = 0;
            while (p < blen && body[p] != 0) {
                const char field = char(body[p++]);
                const std::string val(reinterpret_cast<const char*>(body + p));
                p += val.size() + 1;
                if (field == 'M') r.error = val;
            }
        }
        off += 1 + len;
    }
    return r;
}

// standard_conforming_strings is on: backslashes are literal.
std::string lit(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\0') continue;
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

std::string vec_lit(const std::vector<float>& v) {
    std::string out = "'[";
    char buf[32];
    for (std::size_t i = 0; i < v.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%.9g", double(v[i]));
        if (i) out.push_back(',');
        out += buf;
    }
    out += "]'";
    return out;
}

std::string runtime_dir() {
    if (const char* env = std::getenv("GLCAD_PGLITE_RUNTIME"); env && *env)
        return env;
#ifdef GLCAD_PGLITE_RUNTIME_DIR
    return GLCAD_PGLITE_RUNTIME_DIR;
#else
    return {};
#endif
}

gcm::u64 mtime_key(const fs::file_time_type& t) {
    return gcm::u64(t.time_since_epoch().count());
}

bool ingestible(const fs::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".txt" || ext == ".md" || ext == ".csv";
}

}

PgliteStore::PgliteStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

bool PgliteStore::ready() {
    std::lock_guard<std::mutex> lock(mu_);
    return open_locked();
}

bool PgliteStore::open_locked() {
    const std::string runtime = runtime_dir();
    if (runtime.empty()) return false;
    const std::string pgdata = (fs::path(dir_) / ".pgdata").string();
    if (!Engine::the().open(runtime, pgdata)) return false;
    if (schema_ok_) return true;
    const auto r = run_sql(
        "CREATE EXTENSION IF NOT EXISTS vector;"
        "CREATE TABLE IF NOT EXISTS documents ("
        "  name text PRIMARY KEY,"
        "  title text NOT NULL DEFAULT '',"
        "  source text NOT NULL DEFAULT '',"
        "  mtime bigint NOT NULL DEFAULT 0,"
        "  kind text NOT NULL DEFAULT 'saved');"
        "CREATE TABLE IF NOT EXISTS chunks ("
        "  id serial PRIMARY KEY,"
        "  doc text NOT NULL REFERENCES documents(name) ON DELETE CASCADE,"
        "  seq integer NOT NULL,"
        "  body text NOT NULL);"
        "CREATE TABLE IF NOT EXISTS vectors ("
        "  chunk_id integer PRIMARY KEY REFERENCES chunks(id) ON DELETE CASCADE,"
        "  vec vector NOT NULL);"
        "CREATE TABLE IF NOT EXISTS meta (k text PRIMARY KEY, v text NOT NULL);");
    schema_ok_ = r.ok;
    return schema_ok_;
}

bool PgliteStore::sync_files_locked() {
    std::vector<std::pair<std::string, gcm::u64>> now;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        if (!e.is_regular_file() || !ingestible(e.path())) continue;
        now.emplace_back(e.path().filename().string(),
                         mtime_key(fs::last_write_time(e.path(), ec)));
    }

    const auto known = run_sql("SELECT name, mtime FROM documents WHERE kind = 'file';");
    if (!known.ok) return false;

    for (const auto& row : known.rows) {
        const bool still_here =
            std::any_of(now.begin(), now.end(),
                        [&](const auto& f) { return f.first == row[0]; });
        if (!still_here)
            run_sql("DELETE FROM documents WHERE name = " + lit(row[0]) + ";");
    }

    for (const auto& [name, mt] : now) {
        // Signed: libstdc++'s file_clock epoch is in the future, so counts go negative.
        const std::string mt_str = std::to_string(gcm::i64(mt));
        const bool fresh =
            std::any_of(known.rows.begin(), known.rows.end(), [&](const auto& row) {
                return row[0] == name && row[1] == mt_str;
            });
        if (fresh) continue;
        std::ifstream f(fs::path(dir_) / name);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        std::string sql = "DELETE FROM documents WHERE name = " + lit(name) + ";" +
                          "INSERT INTO documents (name, mtime, kind) VALUES (" +
                          lit(name) + ", " + mt_str + ", 'file');";
        int seq = 0;
        for (const auto& piece : chunk_text(text))
            sql += "INSERT INTO chunks (doc, seq, body) VALUES (" + lit(name) +
                   ", " + std::to_string(seq++) + ", " + lit(piece) + ");";
        if (!run_sql(sql).ok) return false;
    }
    return true;
}

std::string PgliteStore::save_impl(const std::string& title, const std::string& text,
                                   const std::string& source) {
    if (title.empty() || text.empty()) return {};
    std::lock_guard<std::mutex> lock(mu_);
    if (!open_locked()) return {};

    const std::string name = slug_from(title) + ".md";
    std::string content = "# " + title + "\n\n";
    if (!source.empty()) content += "Source: " + source + "\n\n";
    content += text.substr(0, 200000) + "\n";

    std::string sql = "DELETE FROM documents WHERE name = " + lit(name) + ";" +
                      "INSERT INTO documents (name, title, source, kind) VALUES (" +
                      lit(name) + ", " + lit(title) + ", " + lit(source) +
                      ", 'saved');";
    int seq = 0;
    for (const auto& piece : chunk_text(content))
        sql += "INSERT INTO chunks (doc, seq, body) VALUES (" + lit(name) + ", " +
               std::to_string(seq++) + ", " + lit(piece) + ");";
    if (!run_sql(sql).ok) return {};
    return name;
}

bool PgliteStore::ensure_indexed_locked(const std::string& model_id,
                                        const Embedder& embed) {
    if (model_id.empty() || !embed) return false;
    if (!open_locked() || !sync_files_locked()) return false;

    const auto cur = run_sql("SELECT v FROM meta WHERE k = 'model';");
    if (!cur.ok) return false;
    if (cur.rows.empty() || cur.rows[0][0] != model_id) {
        if (!run_sql("DELETE FROM vectors;"
                     "INSERT INTO meta (k, v) VALUES ('model', " + lit(model_id) +
                     ") ON CONFLICT (k) DO UPDATE SET v = EXCLUDED.v;").ok)
            return false;
    }

    const auto todo = run_sql(
        "SELECT c.id, c.body FROM chunks c"
        " LEFT JOIN vectors v ON v.chunk_id = c.id"
        " WHERE v.chunk_id IS NULL ORDER BY c.id;");
    if (!todo.ok) return false;
    if (todo.rows.empty()) return true;

    std::vector<std::string> texts;
    texts.reserve(todo.rows.size());
    for (const auto& row : todo.rows) texts.push_back(row[1]);
    const auto vecs = embed(texts);
    if (vecs.size() != todo.rows.size()) return false;
    for (const auto& v : vecs)
        if (v.empty()) return false;

    for (std::size_t at = 0; at < vecs.size();) {
        std::string sql = "INSERT INTO vectors (chunk_id, vec) VALUES ";
        const std::size_t end = std::min(at + 64, vecs.size());
        for (std::size_t i = at; i < end; ++i) {
            if (i > at) sql += ", ";
            sql += "(" + todo.rows[i][0] + ", " + vec_lit(vecs[i]) + ")";
        }
        sql += ";";
        if (!run_sql(sql).ok) return false;
        at = end;
    }
    return true;
}

bool PgliteStore::ensure_indexed_impl(const std::string& model_id,
                                      const Embedder& embed) {
    std::lock_guard<std::mutex> lock(mu_);
    return ensure_indexed_locked(model_id, embed);
}

std::vector<Hit> PgliteStore::lookup_impl(const std::string& query, int k,
                                          const std::string& model_id,
                                          const Embedder& embed_query,
                                          bool* used_embeddings) {
    if (used_embeddings) *used_embeddings = false;
    if (query.empty()) return {};
    k = std::clamp(k, 1, 10);

    std::lock_guard<std::mutex> lock(mu_);
    if (!open_locked()) return {};
    const bool indexed = ensure_indexed_locked(model_id, embed_query);
    if (!indexed && !sync_files_locked()) return {};

    if (indexed) {
        const auto qv_all = embed_query({query});
        if (!qv_all.empty() && !qv_all[0].empty()) {
            // Relevance floor 0.25 as cosine distance (1 - similarity): 0.75.
            const std::string q = vec_lit(qv_all[0]);
            const auto r = run_sql(
                "SELECT c.doc, c.body FROM chunks c"
                " JOIN vectors v ON v.chunk_id = c.id"
                " WHERE (v.vec <=> " + q + ") < 0.75"
                " ORDER BY v.vec <=> " + q +
                " LIMIT " + std::to_string(k) + ";");
            if (r.ok) {
                if (used_embeddings) *used_embeddings = true;
                std::vector<Hit> out;
                for (const auto& row : r.rows) out.push_back({row[0], row[1]});
                return out;
            }
        }
    }

    const auto all = run_sql("SELECT doc, body FROM chunks;");
    if (!all.ok) return {};
    const auto q = tokens_of(query);
    std::vector<std::pair<float, std::size_t>> scored;
    for (std::size_t i = 0; i < all.rows.size(); ++i) {
        std::string low = all.rows[i][1];
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        const float score = term_score(low, q);
        if (score > 0) scored.push_back({score, i});
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Hit> out;
    for (int i = 0; i < k && i < int(scored.size()); ++i) {
        const auto& row = all.rows[scored[std::size_t(i)].second];
        out.push_back({row[0], row[1]});
    }
    return out;
}

bool PgliteStore::empty_impl() {
    std::lock_guard<std::mutex> lock(mu_);
    if (open_locked() && sync_files_locked()) {
        const auto r = run_sql("SELECT count(*) FROM documents;");
        if (r.ok && !r.rows.empty()) return r.rows[0][0] == "0";
    }
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec))
        if (e.is_regular_file()) return false;
    return true;
}

}
