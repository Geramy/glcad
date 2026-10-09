#include "ui/document_lock.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace gcapp {
namespace {
bool lease_record(const std::string& record) {
    return record.find("\nGLCAD-LOCK-1\n")!=std::string::npos || record.find("\r\nGLCAD-LOCK-1\r\n")!=std::string::npos;
}
std::string owner_line(const std::string& record) {
    const auto line=record.substr(0,record.find_first_of("\r\n"));
    return line.empty()?"another session":line;
}
}
std::string DocumentLock::identity(const std::string& document) {
    std::error_code ec;
    auto p=std::filesystem::weakly_canonical(std::filesystem::absolute(document,ec),ec);
    return ec?document:p.string();
}
#ifndef _WIN32
static bool same_file(int fd,const std::string& path) {
    struct stat held{},named{};
    return fstat(fd,&held)==0 && lstat(path.c_str(),&named)==0 && S_ISREG(named.st_mode) && held.st_dev==named.st_dev && held.st_ino==named.st_ino;
}
#endif
std::unique_ptr<DocumentLock> DocumentLock::acquire(const std::string& document,std::string& holder) {
    holder.clear();
    auto lease=std::unique_ptr<DocumentLock>(new DocumentLock);
    lease->path_=identity(document)+".lock";
    const char* user=std::getenv("USER");
    if(!user) user=std::getenv("USERNAME");
    holder=user?user:"another session";
#ifdef _WIN32
    auto h=CreateFileA(lease->path_.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) { holder="another session or an unwritable folder"; return {}; }
    lease->handle_=h;
    const bool created=GetLastError()!=ERROR_ALREADY_EXISTS;
    char buf[1024]{}; DWORD count=0; ReadFile(h,buf,sizeof(buf)-1,&count,nullptr);
    std::string existing(buf,count);
    if(!created && !lease_record(existing)) {
        holder=owner_line(existing); CloseHandle(h); lease->handle_=nullptr; return {};
    }
    lease->record_=holder+" (process "+std::to_string(GetCurrentProcessId())+")\nGLCAD-LOCK-1\n";
    SetFilePointer(h,0,nullptr,FILE_BEGIN); SetEndOfFile(h);
    DWORD written=0;
    if(!WriteFile(h,lease->record_.data(),DWORD(lease->record_.size()),&written,nullptr)||written!=lease->record_.size()) return {};
    FlushFileBuffers(h);
#else
    char host[256]{}; gethostname(host,sizeof(host)-1);
    lease->record_=holder+"@"+host+" (process "+std::to_string(getpid())+")\nGLCAD-LOCK-1\n";
    for(int attempt=0;attempt<4;++attempt) {
        bool created=false;
        int fd=open(lease->path_.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(fd>=0) created=true;
        else fd=open(lease->path_.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);
        if(fd<0) { holder="an unreadable lock or unwritable folder"; return {}; }
        if(flock(fd,LOCK_EX|LOCK_NB)!=0) {
            char buf[1024]{}; auto count=pread(fd,buf,sizeof(buf)-1,0); close(fd);
            holder=count>0?std::string(buf,std::size_t(count)):"another session";
            holder=owner_line(holder); return {};
        }
        if(!same_file(fd,lease->path_)) { close(fd); continue; }
        char buf[1024]{}; auto count=pread(fd,buf,sizeof(buf)-1,0);
        std::string existing=count>0?std::string(buf,std::size_t(count)):std::string{};
        if(!created && !lease_record(existing)) {
            holder=existing.empty()?"another session":owner_line(existing); close(fd); return {};
        }
        lease->fd_=fd;
        if(ftruncate(fd,0)!=0 || pwrite(fd,lease->record_.data(),lease->record_.size(),0)!=ssize_t(lease->record_.size()) || fsync(fd)!=0) {
            holder="unable to write document lock"; return {};
        }
        holder.clear(); return lease;
    }
    holder="document lock changed during open"; return {};
#endif
    holder.clear(); return lease;
}
bool DocumentLock::owns(const std::string& document) const {
    if(path_!=identity(document)+".lock") return false;
#ifdef _WIN32
    return handle_!=nullptr;
#else
    if(fd_<0 || !same_file(fd_,path_)) return false;
    std::string contents(record_.size(),0);
    return pread(fd_,contents.data(),contents.size(),0)==ssize_t(contents.size()) && contents==record_;
#endif
}
DocumentLock::~DocumentLock() {
#ifdef _WIN32
    if(handle_) {
        FILE_DISPOSITION_INFO info{TRUE};
        SetFileInformationByHandle(handle_,FileDispositionInfo,&info,sizeof(info));
        CloseHandle(handle_);
    }
#else
    if(fd_>=0) {
        std::string current(record_.size(),0);
        if(same_file(fd_,path_) && pread(fd_,current.data(),current.size(),0)==ssize_t(current.size()) && current==record_) unlink(path_.c_str());
        close(fd_);
    }
#endif
}
}
