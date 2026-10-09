#include "ui/shell.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#include "engine/document_file.hpp"

namespace gcapp {

std::string Shell::autosave_path() const {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(gfile::app_config_path()).parent_path() / "recover";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (document_path_.empty()) return (dir / ("untitled-" + recovery_id_ + ".glcad")).string();
    gcm::u64 h = 1469598103934665603ull;
    for (const char ch : document_path_) {
        h ^= static_cast<unsigned char>(ch);
        h *= 1099511628211ull;
    }
    char suffix[24];
    std::snprintf(suffix, sizeof(suffix), "-%016llx.glcad",
                  static_cast<unsigned long long>(h));
    return (dir / (file_title(document_path_) + suffix)).string();
}

std::string Shell::recoverable_path() const {
    namespace fs = std::filesystem;
    const std::string own = autosave_path();
    std::error_code ec;
    if (fs::exists(own, ec) || !document_path_.empty()) return own;
    fs::path newest;
    fs::file_time_type when = fs::file_time_type::min();
    for (fs::directory_iterator it(fs::path(own).parent_path(), ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".glcad" || !it->is_regular_file(ec)) continue;
        const auto time = it->last_write_time(ec);
        if (!ec && time > when) { newest = it->path(); when = time; }
    }
    return newest.empty() ? own : newest.string();
}

bool Shell::prepare_suspend() {
    bool ok=autosave_now() || !document_dirty();
    for (std::size_t i=0; i<tabs_.size(); ++i) {
        if (int(i)==active_tab_) continue;
        swap_with(tabs_[i]);
        const bool saved=autosave_now() || !document_dirty();
        swap_with(tabs_[i]);
        ok = saved && ok;
    }
    invalidate_frame_caches();
    if (!ok) log_.push_back("Could not save recovery for every document. Save a copy before closing.");
    return ok;
}

bool Shell::autosave_now() {
    cached_block_revision_ = ~0ull;
    note_edits();
    if (!document_dirty()) return false;
    const std::string path = autosave_path();
    gfile::Contents now = document_contents();

    bool ok = false;
    if (recovery_written_) {
        const gfile::Delta d = gfile::diff(recovery_base_, now);
        if (d.empty()) {
            autosave_at_ = now_seconds();
            autosave_serial_ = document_serial();
            return true;
        }
        gcm::u64 grown = 0;
        const gfile::Error e = gfile::append_delta(path, d, &grown);
        ok = e.ok;
        if (!ok) {
            recovery_written_ = false;
        }
    }
    if (!ok) {
        const gfile::Error e = gfile::save_file(path, now);
        ok = e.ok;
        recovery_written_ = ok;
    }
    if (ok) {
        recovery_base_ = std::move(now);
        autosave_at_ = now_seconds();
        autosave_serial_ = document_serial();
    }
    return ok;
}

void Shell::autosave_tick() {
    if (!config_loaded_) return;
    note_edits();
    if (!document_dirty()) return;
    if (document_serial() == autosave_serial_) return;
    if (now_seconds() - autosave_at_ < 30.0) return;
    autosave_now();
}

bool Shell::recover_autosave() {
    namespace fs = std::filesystem;
    const std::string path = recoverable_path();
    std::error_code ec;
    if (!fs::exists(path, ec)) { log_.push_back("nothing to recover"); return false; }
    const std::string target = document_path_;
    const bool protected_target = read_only_;
    const std::string holder = lock_holder_;
    const auto original_stamp=document_stamp_; const bool original_change=external_change_;
    auto original_lock=std::move(document_lock_);
    if (!open_file(path)) { document_lock_=std::move(original_lock); return false; }
    document_lock_=std::move(original_lock);
    document_stamp_=original_stamp; external_change_=original_change;
    read_only_ = protected_target;
    lock_holder_ = holder;
    recovered_from_ = path;
    document_path_ = target;
    tab_title_ = target.empty() ? std::string("Recovered") : file_title(target);
    saved_serial_ = document_serial() + 1;
    log_.push_back("recovered the unsaved drawing -- save it to keep it");
    return true;
}
}
