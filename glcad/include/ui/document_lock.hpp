#pragma once
#include <memory>
#include <string>

namespace gcapp {
class DocumentLock {
public:
    static std::unique_ptr<DocumentLock> acquire(const std::string& document, std::string& holder);
    static std::string identity(const std::string& document);
    ~DocumentLock();
    bool owns(const std::string& document) const;
    DocumentLock(const DocumentLock&)=delete;
    DocumentLock& operator=(const DocumentLock&)=delete;
private:
    DocumentLock()=default;
    std::string path_;
    std::string record_;
#ifdef _WIN32
    void* handle_{nullptr};
#else
    int fd_{-1};
#endif
};
}
