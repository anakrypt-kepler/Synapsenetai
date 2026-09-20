#include "web/curl_fetch.h"

#include <cassert>
#include <string>

int main() {
    synapse::web::CurlFetchOptions opt;
    opt.requireSocks = true;
    opt.timeoutSeconds = 1;
    const auto r = synapse::web::curlFetch("https://example.invalid/", opt);
    assert(r.exitCode != 0);
    assert(r.body.empty());
    assert(r.error.find("socks") != std::string::npos);
    return 0;
}
