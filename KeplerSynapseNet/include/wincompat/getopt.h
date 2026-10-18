#pragma once

// Enough of getopt_long for the node CLI. The desktop app does not parse argv
// this way; the symbols exist so the shared sources still compile with MSVC.

#include <cstring>

inline int opterr = 1;
inline int optind = 1;
inline int optopt = 0;
inline char* optarg = nullptr;

enum { no_argument = 0, required_argument = 1, optional_argument = 2 };

struct option {
    const char* name;
    int has_arg;
    int* flag;
    int val;
};

inline int getopt_long(int argc, char* const argv[], const char* optstring,
                       const struct option* longopts, int* longindex) {
    optarg = nullptr;
    if (optind >= argc) return -1;
    const char* arg = argv[optind];
    if (!arg || arg[0] != '-' || arg[1] == '\0') return -1;
    if (arg[1] == '-' && arg[2] == '\0') {
        ++optind;
        return -1;
    }
    if (arg[1] == '-') {
        const char* name = arg + 2;
        const char* eq = std::strchr(name, '=');
        const size_t nlen = eq ? static_cast<size_t>(eq - name) : std::strlen(name);
        for (int i = 0; longopts && longopts[i].name; ++i) {
            if (std::strncmp(longopts[i].name, name, nlen) == 0 &&
                std::strlen(longopts[i].name) == nlen) {
                if (longindex) *longindex = i;
                ++optind;
                if (longopts[i].has_arg == required_argument) {
                    if (eq) optarg = const_cast<char*>(eq + 1);
                    else if (optind < argc) optarg = argv[optind++];
                    else {
                        optopt = longopts[i].val;
                        return '?';
                    }
                }
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }
        ++optind;
        return '?';
    }
    const char flag = arg[1];
    const char* spec = std::strchr(optstring, flag);
    ++optind;
    if (!spec) {
        optopt = flag;
        return '?';
    }
    if (spec[1] == ':') {
        if (arg[2]) optarg = const_cast<char*>(arg + 2);
        else if (optind < argc) optarg = argv[optind++];
        else {
            optopt = flag;
            return '?';
        }
    }
    return flag;
}
