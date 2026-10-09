#include "engine/stream.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gio {

namespace {

template <class T>
void put_le(u8* p, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i)
        p[i] = static_cast<u8>((v >> (8 * i)) & 0xFF);
}
template <class T>
T get_le(const u8* p) {
    T v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<T>(p[i]) << (8 * i);
    return v;
}

}

bool Reader::read_exact(u8* dst, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const std::size_t r = read(dst + got, n - got);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool Reader::read_all(std::vector<u8>& out) {
    u8 buf[65536];
    for (;;) {
        const std::size_t r = read(buf, sizeof(buf));
        if (r == 0) break;
        out.insert(out.end(), buf, buf + r);
    }
    return true;
}

bool Reader::read_u8(u8& v) { return read_exact(&v, 1); }
bool Reader::read_u16(u16& v) {
    u8 b[2];
    if (!read_exact(b, 2)) return false;
    v = get_le<u16>(b);
    return true;
}
bool Reader::read_u32(u32& v) {
    u8 b[4];
    if (!read_exact(b, 4)) return false;
    v = get_le<u32>(b);
    return true;
}
bool Reader::read_u64(u64& v) {
    u8 b[8];
    if (!read_exact(b, 8)) return false;
    v = get_le<u64>(b);
    return true;
}

bool Writer::write_all(const u8* src, std::size_t n) {
    std::size_t put = 0;
    while (put < n) {
        const std::size_t w = write(src + put, n - put);
        if (w == 0) return false;
        put += w;
    }
    return true;
}

bool Writer::write_u8(u8 v) { return write_all(&v, 1); }
bool Writer::write_u16(u16 v) {
    u8 b[2];
    put_le(b, v);
    return write_all(b, 2);
}
bool Writer::write_u32(u32 v) {
    u8 b[4];
    put_le(b, v);
    return write_all(b, 4);
}
bool Writer::write_u64(u64 v) {
    u8 b[8];
    put_le(b, v);
    return write_all(b, 8);
}

std::size_t MemoryWriter::write(const u8* src, std::size_t n) {
    buf_.insert(buf_.end(), src, src + n);
    return n;
}

std::size_t MemoryReader::read(u8* dst, std::size_t n) {
    const std::size_t left = buf_.size() - pos_;
    const std::size_t take = n < left ? n : left;
    if (take) std::memcpy(dst, buf_.data() + pos_, take);
    pos_ += take;
    return take;
}

bool MemoryReader::seek(u64 p) {
    if (p > buf_.size()) return false;
    pos_ = static_cast<std::size_t>(p);
    return true;
}

FileWriter::~FileWriter() {
    if (f_) close();
}

Status FileWriter::open(const std::string& path, bool atomic) {
    close();
    path_ = path;
    atomic_ = atomic;
    temp_ = atomic ? path + ".part" : path;
#ifdef _WIN32
    f_ = _wfopen(std::filesystem::u8path(temp_).c_str(), L"wb");
#else
    f_ = std::fopen(temp_.c_str(), "wb");
#endif
    failed_ = false;
    if (!f_) return Status::fail("cannot open " + temp_ + " for writing");
    written_ = 0;
    return Status::good();
}

std::size_t FileWriter::write(const u8* src, std::size_t n) {
    if (!f_) return 0;
    const std::size_t w = std::fwrite(src, 1, n, f_);
    written_ += w;
    if (w != n) failed_ = true;
    return w;
}

bool FileWriter::flush() {
    if (!f_) return false;
    if (std::fflush(f_) != 0) failed_ = true;
    return !failed_;
}

Status FileWriter::close() {
    if (!f_) return Status::good();
    const bool flushed = flush();
    const bool closed = std::fclose(f_) == 0;
    f_ = nullptr;
    if (!flushed || !closed) return Status::fail("failed to flush " + temp_);

    // One rename; removing the destination first loses it if the rename fails.
    if (atomic_ && temp_ != path_) {
#ifdef _WIN32
        if (!MoveFileExW(std::filesystem::u8path(temp_).c_str(), std::filesystem::u8path(path_).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
        if (std::rename(temp_.c_str(), path_.c_str()) != 0)
#endif
            return Status::fail("failed to commit " + path_);
    }
    return Status::good();
}

FileReader::~FileReader() { close(); }

Status FileReader::open(const std::string& path) {
    close();
#ifdef _WIN32
    f_ = _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
#else
    f_ = std::fopen(path.c_str(), "rb");
#endif
    if (!f_) return Status::fail("cannot open " + path);
    std::fseek(f_, 0, SEEK_END);
    size_ = std::ftell(f_);
    std::fseek(f_, 0, SEEK_SET);
    return Status::good();
}

void FileReader::close() {
    if (f_) std::fclose(f_);
    f_ = nullptr;
    size_ = -1;
}

std::size_t FileReader::read(u8* dst, std::size_t n) {
    return f_ ? std::fread(dst, 1, n, f_) : 0;
}

bool FileReader::eof() const {
    if (!f_) return true;
    const long p = std::ftell(f_);
    return size_ >= 0 && p >= size_;
}

u64 FileReader::position() const {
    return f_ ? static_cast<u64>(std::ftell(f_)) : 0;
}

bool FileReader::seek(u64 p) {
    return f_ && std::fseek(f_, static_cast<long>(p), SEEK_SET) == 0;
}

}
