#pragma once
#include "synapse_msvc_prefix.h"
#include <cstring>
#include <string>

// Small FindFirstFile stand-in so the model folder scan can see .gguf files.

struct dirent {
    char d_name[260];
};

struct DIR {
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA data{};
    dirent entry{};
    bool pending = false;
};

inline DIR* opendir(const char* path) {
    if (!path || !path[0]) return nullptr;
    std::string pattern = path;
    if (pattern.back() != '\\' && pattern.back() != '/') pattern.push_back('\\');
    pattern.push_back('*');
    auto* dir = new DIR();
    dir->handle = FindFirstFileA(pattern.c_str(), &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        delete dir;
        return nullptr;
    }
    dir->pending = true;
    return dir;
}

inline dirent* readdir(DIR* dir) {
    if (!dir) return nullptr;
    if (!dir->pending && !FindNextFileA(dir->handle, &dir->data)) return nullptr;
    dir->pending = false;
    std::strncpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name) - 1);
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
    return &dir->entry;
}

inline int closedir(DIR* dir) {
    if (!dir) return -1;
    if (dir->handle != INVALID_HANDLE_VALUE) FindClose(dir->handle);
    delete dir;
    return 0;
}
