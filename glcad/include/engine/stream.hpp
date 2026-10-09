#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "math/config.hpp"

namespace gio {

using gcm::u8;
using gcm::u16;
using gcm::u32;
using gcm::u64;
using gcm::i64;

struct Status {
    bool        ok{true};
    std::string message;
    explicit operator bool() const noexcept { return ok; }
    static Status good() { return {}; }
    static Status fail(std::string m) { return {false, std::move(m)}; }
};

class Reader {
public:
    virtual ~Reader() = default;

    // A short read is not an error; zero means end of stream.
    virtual std::size_t read(u8* dst, std::size_t n) = 0;
    virtual bool        eof() const = 0;
    virtual u64         position() const = 0;

    virtual bool seekable() const { return false; }
    virtual bool seek(u64) { return false; }
    virtual i64  size() const { return -1; }   // -1: not known in advance

    bool read_exact(u8* dst, std::size_t n);
    bool read_all(std::vector<u8>& out);

    bool read_u8(u8& v);
    bool read_u16(u16& v);        // little-endian on the wire, always
    bool read_u32(u32& v);
    bool read_u64(u64& v);
};

class Writer {
public:
    virtual ~Writer() = default;

    virtual std::size_t write(const u8* src, std::size_t n) = 0;
    virtual bool        flush() { return true; }
    virtual u64         position() const = 0;

    bool write_all(const u8* src, std::size_t n);
    bool write_all(const std::vector<u8>& v) {
        return v.empty() || write_all(v.data(), v.size());
    }
    bool write_u8(u8 v);
    bool write_u16(u16 v);
    bool write_u32(u32 v);
    bool write_u64(u64 v);
};

class MemoryWriter final : public Writer {
public:
    std::size_t write(const u8* src, std::size_t n) override;
    u64         position() const override { return buf_.size(); }

    const std::vector<u8>& bytes() const noexcept { return buf_; }
    std::vector<u8>&       bytes() noexcept { return buf_; }
    void                   clear() noexcept { buf_.clear(); }

private:
    std::vector<u8> buf_;
};

class MemoryReader final : public Reader {
public:
    MemoryReader() = default;
    explicit MemoryReader(std::vector<u8> b) : buf_(std::move(b)) {}
    MemoryReader(const u8* p, std::size_t n) : buf_(p, p + n) {}

    std::size_t read(u8* dst, std::size_t n) override;
    bool        eof() const override { return pos_ >= buf_.size(); }
    u64         position() const override { return pos_; }
    bool        seekable() const override { return true; }
    bool        seek(u64 p) override;
    i64         size() const override { return static_cast<i64>(buf_.size()); }

    const std::vector<u8>& bytes() const noexcept { return buf_; }

private:
    std::vector<u8> buf_;
    std::size_t     pos_{0};
};

class FileWriter final : public Writer {
public:
    FileWriter() = default;
    ~FileWriter() override;

    // Writes to a temporary and renames on close; a crash keeps the old file.
    Status open(const std::string& path, bool atomic = true);
    Status close();

    std::size_t write(const u8* src, std::size_t n) override;
    bool        flush() override;
    u64         position() const override { return written_; }
    bool        is_open() const noexcept { return f_ != nullptr; }

private:
    std::FILE*  f_{nullptr};
    std::string path_, temp_;
    bool        atomic_{true};
    bool        failed_{false};
    u64         written_{0};
};

class FileReader final : public Reader {
public:
    FileReader() = default;
    ~FileReader() override;

    Status open(const std::string& path);
    void   close();

    std::size_t read(u8* dst, std::size_t n) override;
    bool        eof() const override;
    u64         position() const override;
    bool        seekable() const override { return true; }
    bool        seek(u64 p) override;
    i64         size() const override { return size_; }
    bool        is_open() const noexcept { return f_ != nullptr; }

private:
    std::FILE* f_{nullptr};
    i64        size_{-1};
};

class NullWriter final : public Writer {
public:
    std::size_t write(const u8*, std::size_t n) override { n_ += n; return n; }
    u64         position() const override { return n_; }

private:
    u64 n_{0};
};

}
