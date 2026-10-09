#include "test.hpp"

#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "rag/library.hpp"

using namespace grag;

namespace {
std::string fresh_dir() {
    static int n = 0;
    const std::string d = "/tmp/glcad-rag-test-" + std::to_string(getpid()) +
                          "-" + std::to_string(n++);
    std::filesystem::remove_all(d);
    return d;
}
}

TEST(rag_terms_lookup_answers_without_an_embedder) {
    Library lib(fresh_dir());
    CHECK(lib.empty());
    CHECK(!lib.save("Egress", "The minimum egress corridor width is 44 inches.\n\n"
                              "Unrelated paragraph about roofs.", "test").empty());
    CHECK(!lib.empty());
    bool embedded = true;
    const auto hits = lib.lookup("egress corridor width", 3, "", {}, &embedded);
    CHECK(!embedded);
    CHECK_MSG(!hits.empty() && hits[0].text.find("44 inches") != std::string::npos,
              "wrong or missing passage");
    CHECK(lib.lookup("is the of", 3, "", {}).empty());
}

TEST(rag_embeddings_commit_to_sidecars_and_persist) {
    const std::string dir = fresh_dir();
    const auto embed = [](const std::vector<std::string>& texts) {
        std::vector<std::vector<float>> out;
        for (const auto& t : texts) {
            const bool wall = t.find("wall") != std::string::npos;
            out.push_back(wall ? std::vector<float>{1.0f, 0.1f}
                               : std::vector<float>{0.1f, 1.0f});
        }
        return out;
    };
    int calls = 0;
    const auto counting = [&](const std::vector<std::string>& t) {
        ++calls;
        return embed(t);
    };
    {
        Library lib(dir);
        lib.save("Walls", "Exterior wall assemblies are hatched.", "");
        lib.save("Roofs", "Roof pitch tables and rafter spans.", "");
        CHECK(lib.ensure_indexed("test-model", counting));
        bool embedded = false;
        const auto hits = lib.lookup("wall construction", 2, "test-model", counting, &embedded);
        CHECK(embedded);
        CHECK_MSG(!hits.empty() && hits[0].text.find("wall") != std::string::npos,
                  "cosine did not rank the wall passage first");
    }
    {
        Library lib(dir);
        calls = 0;
        CHECK(lib.ensure_indexed("test-model", counting));
        CHECK_MSG(calls == 0, "ensure_indexed re-embedded %d times over a committed index", calls);
        bool embedded = false;
        const auto hits = lib.lookup("roof spans", 2, "test-model", counting, &embedded);
        CHECK(embedded);
        CHECK_MSG(calls == 1, "lookup should embed only the query (calls=%d)", calls);
        CHECK(!hits.empty() && hits[0].text.find("Roof") != std::string::npos);
    }
    {
        Library lib(dir);
        calls = 0;
        CHECK(lib.ensure_indexed("other-model", counting));
        CHECK_MSG(calls == 1, "a new model must re-embed (calls=%d)", calls);
    }
    std::filesystem::remove_all(dir);
}

#ifdef GLCAD_WITH_PGLITE

// One engine per process: all pglite tests share one directory, in order.
namespace {
const std::string& pg_dir() {
    static const std::string d = fresh_dir();
    return d;
}

std::vector<std::vector<float>> topic_embed(const std::vector<std::string>& texts) {
    std::vector<std::vector<float>> out;
    for (const auto& t : texts) {
        std::string low = t;
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        std::vector<float> v{0.0f, 0.0f, 0.0f};
        if (low.find("wall") != std::string::npos) v[0] = 1.0f;
        else if (low.find("roof") != std::string::npos) v[1] = 1.0f;
        else v[2] = 1.0f;
        out.push_back(std::move(v));
    }
    return out;
}
}

TEST(pglite_backend_answers_for_itself) {
    Library lib(pg_dir(), Backend::Postgres);
    CHECK(lib.backend() == Backend::Postgres);
    CHECK(lib.dir() == pg_dir());
}

TEST(pglite_terms_lookup_answers_without_an_embedder) {
    Library lib(pg_dir(), Backend::Postgres);
    CHECK(lib.empty());
    CHECK(!lib.save("Egress", "The minimum egress corridor width is 44 inches.\n\n"
                              "Unrelated paragraph about attics.", "test").empty());
    CHECK(!lib.empty());
    bool embedded = true;
    const auto hits = lib.lookup("egress corridor width", 3, "", {}, &embedded);
    CHECK(!embedded);
    CHECK_MSG(!hits.empty() && hits[0].text.find("44 inches") != std::string::npos,
              "wrong or missing passage");
    CHECK(lib.lookup("is the of", 3, "", {}).empty());
}

TEST(pglite_ingests_hand_placed_files) {
    {
        std::ofstream f(std::filesystem::path(pg_dir()) / "handbook.md");
        f << "Stair risers shall not exceed 7.75 inches in dwellings.\n";
    }
    Library lib(pg_dir(), Backend::Postgres);
    bool embedded = true;
    const auto hits = lib.lookup("stair risers", 3, "", {}, &embedded);
    CHECK(!embedded);
    CHECK_MSG(!hits.empty() && hits[0].file == "handbook.md",
              "hand-placed file was not ingested");
}

TEST(pglite_embeddings_persist_in_postgres) {
    int calls = 0;
    const auto counting = [&](const std::vector<std::string>& t) {
        ++calls;
        return topic_embed(t);
    };
    {
        Library lib(pg_dir(), Backend::Postgres);
        lib.save("Walls", "Exterior wall assemblies are hatched.", "");
        lib.save("Roofs", "Roof pitch tables and rafter spans.", "");
        CHECK(lib.ensure_indexed("test-model", counting));
        bool embedded = false;
        const auto hits =
            lib.lookup("wall construction", 2, "test-model", counting, &embedded);
        CHECK(embedded);
        CHECK_MSG(!hits.empty() && hits[0].text.find("wall") != std::string::npos,
                  "cosine did not rank the wall passage first");
        CHECK_MSG(hits.size() == 1, "the relevance floor leaked %d hits",
                  int(hits.size()));
    }
    {
        Library lib(pg_dir(), Backend::Postgres);
        calls = 0;
        CHECK(lib.ensure_indexed("test-model", counting));
        CHECK_MSG(calls == 0, "ensure_indexed re-embedded %d times over a stored index", calls);
        bool embedded = false;
        const auto hits =
            lib.lookup("roof spans", 2, "test-model", counting, &embedded);
        CHECK(embedded);
        CHECK_MSG(calls == 1, "lookup should embed only the query (calls=%d)", calls);
        CHECK(!hits.empty() && hits[0].text.find("Roof") != std::string::npos);
    }
    {
        Library lib(pg_dir(), Backend::Postgres);
        calls = 0;
        CHECK(lib.ensure_indexed("other-model", counting));
        CHECK_MSG(calls == 1, "a new model must re-embed (calls=%d)", calls);
    }
}

TEST(pglite_unservable_directory_falls_back_to_working_sidecars) {
    Library lib(fresh_dir(), Backend::Postgres);
    CHECK(lib.backend() == Backend::Sidecar);
    CHECK(!lib.save("Ramps", "Ramp slope shall not exceed 1:12.", "test").empty());
    const auto hits = lib.lookup("ramp slope", 3, "", {});
    CHECK_MSG(!hits.empty() && hits[0].text.find("1:12") != std::string::npos,
              "the fallback library did not answer");
}

#endif
