#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <functional>
#include <stdexcept>
#include <cstdio>
#include <direct.h>

class Mutex {
public:
    CRITICAL_SECTION cs;
    Mutex() { InitializeCriticalSection(&cs); }
    ~Mutex() { DeleteCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};

class LockGuard {
private:
    Mutex& m;
public:
    LockGuard(Mutex& m) : m(m) { m.lock(); }
    ~LockGuard() { m.unlock(); }
};

class CondVar {
private:
    CONDITION_VARIABLE cv;
public:
    CondVar() { InitializeConditionVariable(&cv); }
    void wait(Mutex& m) {
        SleepConditionVariableCS(&cv, &m.cs, INFINITE);
    }
    void notify_all() {
        WakeAllConditionVariable(&cv);
    }
};

class Thread {
private:
    HANDLE handle{nullptr};
    std::function<void()> func;

    static DWORD WINAPI run(LPVOID lpParam) {
        auto* f = static_cast<std::function<void()>*>(lpParam);
        (*f)();
        return 0;
    }

public:
    Thread() = default;
    template<typename F>
    explicit Thread(F&& f) : func(std::forward<F>(f)) {
        handle = CreateThread(nullptr, 0, run, &func, 0, nullptr);
    }
    ~Thread() {
        if (handle) {
            CloseHandle(handle);
        }
    }
    void join() {
        if (handle) {
            WaitForSingleObject(handle, INFINITE);
            CloseHandle(handle);
            handle = nullptr;
        }
    }
};

namespace fs_compat {
    inline bool exists(const std::string& path) {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }

    inline void create_directories(const std::string& path) {
        std::string current_level = "";
        std::string path_copy = path;
        for (char& c : path_copy) {
            if (c == '\\') c = '/';
        }

        size_t pos = 0;
        while ((pos = path_copy.find('/', pos)) != std::string::npos) {
            std::string dir = path_copy.substr(0, pos);
            if (!dir.empty() && !exists(dir)) {
                _mkdir(dir.c_str());
            }
            pos++;
        }
        if (!exists(path_copy)) {
            _mkdir(path_copy.c_str());
        }
    }

    inline void remove_all(const std::string& path) {
        if (!exists(path)) return;
        DIR* dir = opendir(path.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name != "." && name != "..") {
                    std::string full_path = path + "/" + name;
                    struct stat st;
                    if (stat(full_path.c_str(), &st) == 0) {
                        if (S_ISDIR(st.st_mode)) {
                            remove_all(full_path);
                        } else {
                            std::remove(full_path.c_str());
                        }
                    }
                }
            }
            closedir(dir);
            _rmdir(path.c_str());
        } else {
            std::remove(path.c_str());
        }
    }

    inline void remove(const std::string& path) {
        std::remove(path.c_str());
    }

    inline std::vector<std::string> get_db_files(const std::string& db_dir) {
        std::vector<std::string> files;
        DIR* dir = opendir(db_dir.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name.length() > 3 && name.substr(name.length() - 3) == ".db") {
                    files.push_back(name);
                }
            }
            closedir(dir);
        }
        return files;
    }
}

#endif // _WIN32
#endif // COMPAT_H
