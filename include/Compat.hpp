#ifndef MEALWALLET_COMPAT_HPP
#define MEALWALLET_COMPAT_HPP

#include <string>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <direct.h>
#else
  #include <dirent.h>
  #include <mutex>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <thread>
  #include <unistd.h>
#endif

namespace mealwallet {
namespace compat {

/**
 * Portable Recursive Mutex:
 * Uses Win32 CRITICAL_SECTION on Windows (works even on MinGW win32-thread builds
 * where std::recursive_mutex is unavailable) and std::recursive_mutex on POSIX.
 */
class RecursiveMutex {
#ifdef _WIN32
    CRITICAL_SECTION cs_;
public:
    RecursiveMutex()  { InitializeCriticalSection(&cs_); }
    ~RecursiveMutex() { DeleteCriticalSection(&cs_); }
    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;
    void lock()   { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
#else
    std::recursive_mutex mtx_;
public:
    void lock()   { mtx_.lock(); }
    void unlock() { mtx_.unlock(); }
#endif
};

class LockGuard {
    RecursiveMutex& mtx_;
public:
    explicit LockGuard(RecursiveMutex& m) : mtx_(m) { mtx_.lock(); }
    ~LockGuard() { mtx_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

inline void ensureDirectory(const std::string& path) {
    if (path.empty()) return;
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

inline void removeDirectoryRecursive(const std::string& path) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    std::string pattern = path + "\\*";
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string name(fd.cFileName);
            if (name == "." || name == "..") continue;
            std::string full = path + "\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeDirectoryRecursive(full);
            } else {
                DeleteFileA(full.c_str());
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryA(path.c_str());
#else
    DIR* dir = opendir(path.c_str());
    if (!dir) return;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        std::string full = path + "/" + name;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            removeDirectoryRecursive(full);
        } else {
            unlink(full.c_str());
        }
    }
    closedir(dir);
    rmdir(path.c_str());
#endif
}

template <typename Func>
inline void spawnDetached(Func fn) {
#ifdef _WIN32
    struct ThreadCtx {
        Func f;
        static DWORD WINAPI entry(LPVOID param) {
            ThreadCtx* c = static_cast<ThreadCtx*>(param);
            c->f();
            delete c;
            return 0;
        }
    };
    ThreadCtx* ctx = new ThreadCtx{std::move(fn)};
    HANDLE h = CreateThread(nullptr, 0, &ThreadCtx::entry, ctx, 0, nullptr);
    if (h) CloseHandle(h);
    else delete ctx;
#else
    std::thread(std::move(fn)).detach();
#endif
}

} // namespace compat
} // namespace mealwallet

#endif // MEALWALLET_COMPAT_HPP
