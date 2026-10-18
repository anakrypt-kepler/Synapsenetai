#pragma once
#include "synapse_msvc_prefix.h"

inline int poll(WSAPOLLFD* fds, unsigned long count, int timeoutMs) {
    return WSAPoll(fds, count, timeoutMs);
}
