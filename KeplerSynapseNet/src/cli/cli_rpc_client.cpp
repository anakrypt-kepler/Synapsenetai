#include "cli/cli_rpc_client.h"

#include "config/config_loader.h"
#include "rpc/rpc_commands.h"
#include "utils/utils.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace synapse {

using json = nlohmann::json;

static bool rpcHttpPost(uint16_t port,
                        const std::string& body,
                        std::string& responseBodyOut,
                        std::string& errorOut,
                        int timeoutSeconds,
                        const std::string& authorizationHeader) {
    responseBodyOut.clear();
    errorOut.clear();

    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        errorOut = "socket() failed";
        return false;
    }

    struct timeval tv;
    tv.tv_sec = timeoutSeconds;
    tv.tv_usec = 0;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(sock);
        errorOut = "connect() failed";
        return false;
    }

    std::ostringstream req;
    req << "POST / HTTP/1.1\r\n";
    req << "Host: 127.0.0.1\r\n";
    req << "Content-Type: application/json\r\n";
    if (!authorizationHeader.empty()) {
        req << "Authorization: " << authorizationHeader << "\r\n";
    }
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    req << "\r\n";
    req << body;
    std::string reqStr = req.str();

    size_t sent = 0;
    while (sent < reqStr.size()) {
        ssize_t n = ::send(sock, reqStr.data() + sent, reqStr.size() - sent, 0);
        if (n <= 0) {
            ::close(sock);
            errorOut = "send() failed";
            return false;
        }
        sent += static_cast<size_t>(n);
    }

    std::string resp;
    resp.reserve(8192);
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        resp.append(buf, buf + n);
        if (resp.size() > 8 * 1024 * 1024) {
            ::close(sock);
            errorOut = "response too large";
            return false;
        }
        size_t headerEnd = resp.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }

        size_t clPos = resp.find("Content-Length:");
        if (clPos == std::string::npos) {
            continue;
        }
        size_t clEnd = resp.find("\r\n", clPos);
        if (clEnd == std::string::npos) {
            continue;
        }
        std::string clStr = resp.substr(clPos + 15, clEnd - (clPos + 15));
        size_t contentLength = 0;
        try {
            contentLength = static_cast<size_t>(std::stoul(clStr));
        } catch (...) {
            ::close(sock);
            errorOut = "invalid Content-Length";
            return false;
        }

        size_t bodyStart = headerEnd + 4;
        if (resp.size() >= bodyStart + contentLength) {
            responseBodyOut = resp.substr(bodyStart, contentLength);
            ::close(sock);
            return true;
        }
    }

    ::close(sock);
    errorOut = "no response";
    return false;
}

static bool rpcCall(uint16_t port,
                    const std::string& method,
                    const json& params,
                    json& resultOut,
                    std::string& errorOut,
                    int timeoutSeconds,
                    const std::string& authorizationHeader) {
    json req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = method;
    req["params"] = params;

    std::string respBody;
    if (!rpcHttpPost(port, req.dump(), respBody, errorOut, timeoutSeconds, authorizationHeader)) {
        return false;
    }

    json resp;
    try {
        resp = json::parse(respBody);
    } catch (const std::exception& e) {
        errorOut = std::string("invalid JSON response: ") + e.what();
        return false;
    }

    if (resp.contains("error") && !resp["error"].is_null()) {
        try {
            int code = resp["error"].value("code", -1);
            std::string msg = resp["error"].value("message", "RPC error");
            errorOut = "rpc_error(" + std::to_string(code) + "): " + msg;
        } catch (...) {
            errorOut = "rpc_error";
        }
        return false;
    }
    if (!resp.contains("result")) {
        errorOut = "missing result field";
        return false;
    }
    resultOut = resp["result"];
    return true;
}

static bool isRpcTransportError(const std::string& err) {
    if (err == "socket() failed" || err == "connect() failed" || err == "send() failed" || err == "no response") {
        return true;
    }
    if (err.rfind("invalid JSON response:", 0) == 0) {
        return true;
    }
    return err == "missing result field";
}

using CliCallFn = std::function<bool(const std::string&, const json&, json&, std::string&)>;
using CliTransportErrorFn = std::function<bool(const std::string&)>;

static std::optional<int> runCliWithExecutor(const NodeConfig& config,
                                             const CliCallFn& callFn,
                                             const CliTransportErrorFn& transportErrorFn) {
    if (config.commandArgs.empty()) {
        return 0;
    }

    const std::string cmd = config.commandArgs[0];
    auto call = callFn;
    auto isRpcTransportError = transportErrorFn;

    if (cmd == "address") {
        json out;
        std::string err;
        if (!call("wallet.address", json::object(), out, err)) {
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        std::cout << out.value("address", "") << "\n";
        return 0;
    }

    if (cmd == "balance") {
        json out;
        std::string err;
        if (!call("wallet.balance", json::object(), out, err)) {
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        const uint64_t balanceAtoms = out.value("balanceAtoms", static_cast<uint64_t>(0));
        std::cout << "address=" << out.value("address", "") << "\n";
        std::cout << "balance=" << utils::Formatter::formatCurrency(balanceAtoms) << "\n";
        return 0;
    }

    if (cmd == "naan") {
        if (config.commandArgs.size() == 1 || config.commandArgs[1] == "help") {
            std::cout << "Usage:\n";
            std::cout << "  synapsed naan status\n";
            std::cout << "  synapsed naan artifacts [--since TIMESTAMP] [--limit N]\n";
            std::cout << "  synapsed naan artifact <hashHex>\n";
            std::cout << "  synapsed naan drafts [--status S] [--limit N] [--include-rejected 0|1]\n";
            std::cout << "  synapsed naan draft <draftIdHex>\n";
            std::cout << "  synapsed naan dryrun [--limit N]\n";
            std::cout << "  synapsed naan drain [--limit N]\n";
            return 0;
        }

        const std::string sub = config.commandArgs[1];
        if (sub == "status") {
            json out;
            std::string err;
            if (!call("naan.status", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "artifacts") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--since" && i + 1 < config.commandArgs.size()) {
                    params["since"] = std::stoll(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--limit" && i + 1 < config.commandArgs.size()) {
                    params["limit"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                }
            }

            json out;
            std::string err;
            if (!call("naan.observatory.artifacts", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "artifact") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed naan artifact <hashHex>\n";
                return 1;
            }
            json params;
            params["hash"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("naan.observatory.artifact.get", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "drafts") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--status" && i + 1 < config.commandArgs.size()) {
                    params["status"] = config.commandArgs[i + 1];
                    i++;
                } else if (config.commandArgs[i] == "--limit" && i + 1 < config.commandArgs.size()) {
                    params["limit"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--include-rejected" && i + 1 < config.commandArgs.size()) {
                    params["includeRejected"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                }
            }

            json out;
            std::string err;
            if (!call("naan.observatory.drafts", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "draft") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed naan draft <draftIdHex>\n";
                return 1;
            }
            json params;
            params["draftId"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("naan.observatory.draft.get", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "dryrun") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--limit" && i + 1 < config.commandArgs.size()) {
                    params["limit"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                }
            }

            json out;
            std::string err;
            if (!call("naan.pipeline.dryrun", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "drain") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--limit" && i + 1 < config.commandArgs.size()) {
                    params["limit"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                }
            }

            json out;
            std::string err;
            if (!call("naan.pipeline.drain", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        std::cerr << "Unknown naan subcommand: " << sub << "\n";
        return 1;
    }

    if (cmd == "tor") {
        if (config.commandArgs.size() == 1 || config.commandArgs[1] == "help") {
            std::cout << "Usage:\n";
            std::cout << "  synapsed tor status\n";
            std::cout << "  synapsed tor refresh-bridges [--persist-sanitized 0|1]\n";
            std::cout << "  synapsed tor restart-managed [--reload-web 0|1]\n";
            std::cout << "  synapsed tor mode <auto|external|managed> [--socks-host H] [--socks-port N] [--control-port N] [--persist 0|1] [--reload-web 0|1]\n";
            return 0;
        }

        const std::string sub = config.commandArgs[1];
        if (sub == "status") {
            json out;
            std::string err;
            if (!call("node.status", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            json tor;
            tor["torRuntimeMode"] = out.value("torRuntimeMode", "");
            tor["torSocksHost"] = out.value("torSocksHost", "127.0.0.1");
            tor["torSocksPort"] = out.value("torSocksPort", 0);
            tor["torControlPort"] = out.value("torControlPort", 0);
            tor["torControlReachable"] = out.value("torControlReachable", false);
            tor["torSocksReachable"] = out.value("torSocksReachable", false);
            tor["torReadyForWeb"] = out.value("torReadyForWeb", false);
            tor["torReadyForOnion"] = out.value("torReadyForOnion", false);
            tor["torReadyForOnionService"] = out.value("torReadyForOnionService", false);
            tor["torOnionServiceActive"] = out.value("torOnionServiceActive", false);
            tor["torOnionServiceState"] = out.value("torOnionServiceState", "");
            tor["torDegraded"] = out.value("torDegraded", false);
            tor["torManagedPid"] = out.value("torManagedPid", 0);
            tor["torBootstrapState"] = out.value("torBootstrapState", "");
            tor["torBootstrapPercent"] = out.value("torBootstrapPercent", 0);
            tor["torBootstrapReasonCode"] = out.value("torBootstrapReasonCode", "");
            tor["torConflictHint9050"] = out.value("torConflictHint9050", false);
            tor["torBridgeProviderUpdatedAt"] = out.value("torBridgeProviderUpdatedAt", static_cast<uint64_t>(0));
            tor["torBridgeCacheAgeSeconds"] = out.value("torBridgeCacheAgeSeconds", static_cast<uint64_t>(0));
            tor["torBridgeProvider"] = out.value("torBridgeProvider", json::object());
            tor["routeMode"] = out.value("routeMode", "");
            std::cout << tor.dump(2) << "\n";
            return 0;
        }

        json params = json::object();
        if (sub == "refresh" || sub == "refresh-bridges") {
            params["action"] = "refresh_bridges";
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--persist-sanitized" && i + 1 < config.commandArgs.size()) {
                    params["persistSanitized"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                }
            }
        } else if (sub == "restart" || sub == "restart-managed") {
            params["action"] = "restart_managed_tor";
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if ((config.commandArgs[i] == "--reload-web" || config.commandArgs[i] == "--reload-web-config") &&
                    i + 1 < config.commandArgs.size()) {
                    params["reloadWebConfig"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                }
            }
        } else if (sub == "mode") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed tor mode <auto|external|managed> [--socks-host H] [--socks-port N] [--control-port N] [--persist 0|1] [--reload-web 0|1]\n";
                return 1;
            }
            params["action"] = "switch_mode";
            params["mode"] = config.commandArgs[2];
            for (size_t i = 3; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--socks-host" && i + 1 < config.commandArgs.size()) {
                    params["socksHost"] = config.commandArgs[i + 1];
                    i++;
                } else if (config.commandArgs[i] == "--socks-port" && i + 1 < config.commandArgs.size()) {
                    params["socksPort"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--control-port" && i + 1 < config.commandArgs.size()) {
                    params["controlPort"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--persist" && i + 1 < config.commandArgs.size()) {
                    params["persist"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                } else if ((config.commandArgs[i] == "--reload-web" || config.commandArgs[i] == "--reload-web-config") &&
                           i + 1 < config.commandArgs.size()) {
                    params["reloadWebConfig"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                }
            }
        } else {
            std::cerr << "Unknown tor subcommand: " << sub << "\n";
            return 1;
        }

        json out;
        std::string err;
        if (!call("node.tor.control", params, out, err)) {
            if (isRpcTransportError(err)) return std::nullopt;
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (cmd == "poe") {
        if (config.commandArgs.size() == 1 || config.commandArgs[1] == "help") {
            std::cout << "Usage:\n";
            std::cout << "  synapsed poe submit --question Q --answer A [--source S]\n";
            std::cout << "  synapsed poe submit-code --title T (--patch P | --patch-file PATH)\n";
            std::cout << "  synapsed poe list-code [--limit N]\n";
            std::cout << "  synapsed poe fetch-code <submitIdHex|contentIdHex>\n";
            std::cout << "  synapsed poe vote <submitIdHex>\n";
            std::cout << "  synapsed poe finalize <submitIdHex>\n";
            std::cout << "  synapsed poe epoch [--budget NGT] [--iters N]\n";
            std::cout << "  synapsed poe export <path>\n";
            std::cout << "  synapsed poe import <path>\n";
            std::cout << "  synapsed poe pubkey\n";
            std::cout << "  synapsed poe validators\n";
            return 0;
        }

        const std::string sub = config.commandArgs[1];

        if (sub == "pubkey") {
            json out;
            std::string err;
            if (!call("wallet.address", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.value("pubkey", "") << "\n";
            return 0;
        }

        if (sub == "validators") {
            json out;
            std::string err;
            if (!call("poe.validators", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            if (out.is_array()) {
                for (const auto& v : out) {
                    if (v.is_string()) {
                        std::cout << v.get<std::string>() << "\n";
                    }
                }
                if (out.empty()) {
                    std::cout << "(none)\n";
                }
            } else {
                std::cout << "(none)\n";
            }
            return 0;
        }

        if (sub == "submit") {
            std::unordered_map<std::string, std::string> opts;
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i].rfind("--", 0) != 0) {
                    continue;
                }
                std::string k = config.commandArgs[i].substr(2);
                std::string v;
                if (i + 1 < config.commandArgs.size() && config.commandArgs[i + 1].rfind("--", 0) != 0) {
                    v = config.commandArgs[i + 1];
                    i++;
                }
                opts[k] = v;
            }

            std::string q = opts["question"];
            std::string a = opts["answer"];
            std::string s = opts["source"];
            if (q.empty() || a.empty()) {
                std::cerr << "Missing --question/--answer\n";
                return 1;
            }

            json params;
            params["question"] = q;
            params["answer"] = a;
            if (!s.empty()) {
                params["source"] = s;
            }
            params["auto_finalize"] = true;

            json out;
            std::string err;
            if (!call("poe.submit", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << "submitId=" << out.value("submitId", "") << "\n";
            std::cout << "contentId=" << out.value("contentId", "") << "\n";
            const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
            std::cout << "acceptanceReward=" << utils::Formatter::formatCurrency(creditedAtoms) << "\n";
            return 0;
        }

        if (sub == "submit-code") {
            std::unordered_map<std::string, std::string> opts;
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i].rfind("--", 0) != 0) {
                    continue;
                }
                std::string k = config.commandArgs[i].substr(2);
                std::string v;
                if (i + 1 < config.commandArgs.size() && config.commandArgs[i + 1].rfind("--", 0) != 0) {
                    v = config.commandArgs[i + 1];
                    i++;
                }
                opts[k] = v;
            }

            std::string title = opts["title"];
            std::string patch = opts["patch"];
            std::string patchFile = opts["patch-file"];
            if (patch.empty() && !patchFile.empty()) {
                std::ifstream in(patchFile, std::ios::binary);
                if (!in) {
                    std::cerr << "Failed to read --patch-file\n";
                    return 1;
                }
                std::ostringstream ss;
                ss << in.rdbuf();
                patch = ss.str();
            }

            if (title.empty() || patch.empty()) {
                std::cerr << "Missing --title and --patch/--patch-file\n";
                return 1;
            }

            json params;
            params["title"] = title;
            params["patch"] = patch;
            std::string cites = opts["citations"];
            if (!cites.empty()) {
                for (char& c : cites) {
                    if (c == ';') {
                        c = ',';
                    }
                }
                json arr = json::array();
                std::string cur;
                for (size_t i = 0; i <= cites.size(); ++i) {
                    if (i == cites.size() || cites[i] == ',') {
                        std::string t = cur;
                        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
                        while (!t.empty() && isSpace(static_cast<unsigned char>(t.front()))) {
                            t.erase(t.begin());
                        }
                        while (!t.empty() && isSpace(static_cast<unsigned char>(t.back()))) {
                            t.pop_back();
                        }
                        if (!t.empty()) {
                            arr.push_back(t);
                        }
                        cur.clear();
                    } else {
                        cur.push_back(cites[i]);
                    }
                }
                if (!arr.empty()) {
                    params["citations"] = arr;
                }
            }
            params["auto_finalize"] = true;

            json out;
            std::string err;
            if (!call("poe.submit_code", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << "submitId=" << out.value("submitId", "") << "\n";
            std::cout << "contentId=" << out.value("contentId", "") << "\n";
            const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
            std::cout << "acceptanceReward=" << utils::Formatter::formatCurrency(creditedAtoms) << "\n";
            return 0;
        }

        if (sub == "list-code") {
            size_t limit = 25;
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--limit" && i + 1 < config.commandArgs.size()) {
                    limit = static_cast<size_t>(std::max(1, std::stoi(config.commandArgs[i + 1])));
                    i++;
                }
            }
            json params;
            params["limit"] = limit;
            json out;
            std::string err;
            if (!call("poe.list_code", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            if (!out.is_array() || out.empty()) {
                std::cout << "(none)\n";
                return 0;
            }
            for (const auto& item : out) {
                std::string sid = item.value("submitId", "");
                std::string title = item.value("title", "");
                if (!sid.empty()) {
                    std::cout << sid << "  " << title << "\n";
                }
            }
            return 0;
        }

        if (sub == "fetch-code") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed poe fetch-code <submitIdHex|contentIdHex>\n";
                return 1;
            }
            json params;
            params["id"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("poe.fetch_code", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << "submitId=" << out.value("submitId", "") << "\n";
            std::cout << "contentId=" << out.value("contentId", "") << "\n";
            std::cout << "timestamp=" << out.value("timestamp", 0) << "\n";
            std::cout << "title=" << out.value("title", "") << "\n";
            std::cout << "finalized=" << (out.value("finalized", false) ? "true" : "false") << "\n";
            std::cout << "patch:\n";
            std::cout << out.value("patch", "") << "\n";
            return 0;
        }

        if (sub == "vote") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed poe vote <submitIdHex>\n";
                return 1;
            }
            json params;
            params["submitId"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("poe.vote", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::string status = out.value("status", "");
            bool added = out.value("added", false);
            if (!status.empty()) {
                std::cout << status << "\n";
            }
            const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
            if (creditedAtoms > 0) {
                std::cout << "credited=" << utils::Formatter::formatCurrency(creditedAtoms) << "\n";
            }
            return added ? 0 : 1;
        }

        if (sub == "finalize") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed poe finalize <submitIdHex>\n";
                return 1;
            }
            json params;
            params["submitId"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("poe.finalize", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            bool finalized = out.value("finalized", false);
            if (!finalized) {
                std::cerr << "not_finalized\n";
                return 1;
            }
            std::cout << "finalized\n";
            const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
            if (creditedAtoms > 0) {
                std::cout << "credited=" << utils::Formatter::formatCurrency(creditedAtoms) << "\n";
            }
            return 0;
        }

        if (sub == "epoch") {
            auto parseNgtAtomic = [](const std::string& s, uint64_t& out) -> bool {
                if (s.empty()) return false;
                std::string t = s;
                for (auto& c : t) if (c == ',') c = '.';
                size_t dot = t.find('.');
                std::string intPart = dot == std::string::npos ? t : t.substr(0, dot);
                std::string fracPart = dot == std::string::npos ? "" : t.substr(dot + 1);
                if (intPart.empty()) intPart = "0";
                if (fracPart.size() > 8) return false;
                for (char c : intPart) if (c < '0' || c > '9') return false;
                for (char c : fracPart) if (c < '0' || c > '9') return false;
                unsigned __int128 iv = 0;
                for (char c : intPart) iv = iv * 10 + static_cast<unsigned>(c - '0');
                unsigned __int128 fv = 0;
                for (char c : fracPart) fv = fv * 10 + static_cast<unsigned>(c - '0');
                for (size_t i = fracPart.size(); i < 8; ++i) fv *= 10;
                unsigned __int128 total = iv * 100000000ULL + fv;
                if (total > std::numeric_limits<uint64_t>::max()) return false;
                out = static_cast<uint64_t>(total);
                return true;
            };

            uint64_t budgetAtoms = 0;
            uint32_t iters = 20;
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--budget" && i + 1 < config.commandArgs.size()) {
                    uint64_t v = 0;
                    if (!parseNgtAtomic(config.commandArgs[i + 1], v)) {
                        std::cerr << "Invalid --budget\n";
                        return 1;
                    }
                    budgetAtoms = v;
                    i++;
                } else if (config.commandArgs[i] == "--iters" && i + 1 < config.commandArgs.size()) {
                    iters = static_cast<uint32_t>(std::max(1, std::stoi(config.commandArgs[i + 1])));
                    i++;
                }
            }

            json params;
            if (budgetAtoms > 0) {
                params["budget_atoms"] = budgetAtoms;
            }
            params["iters"] = iters;
            json out;
            std::string err;
            if (!call("poe.epoch", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << "epochId=" << out.value("epochId", 0) << "\n";
            std::cout << "allocationHash=" << out.value("allocationHash", "") << "\n";
            const uint64_t mintedAtoms = out.value("mintedAtoms", static_cast<uint64_t>(0));
            std::cout << "minted=" << utils::Formatter::formatCurrency(mintedAtoms) << "\n";
            std::cout << "mintedEntries=" << out.value("mintedEntries", 0) << "\n";
            const uint64_t mintedSelfAtoms = out.value("mintedSelfAtoms", static_cast<uint64_t>(0));
            if (mintedSelfAtoms > 0) {
                std::cout << "youEarned=" << utils::Formatter::formatCurrency(mintedSelfAtoms) << "\n";
            }
            return 0;
        }

        if (sub == "export") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed poe export <path>\n";
                return 1;
            }
            json params;
            params["path"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("poe.export", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump() << "\n";
            return 0;
        }

        if (sub == "import") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed poe import <path>\n";
                return 1;
            }
            json params;
            params["path"] = config.commandArgs[2];
            json out;
            std::string err;
            if (!call("poe.import", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump() << "\n";
            return 0;
        }

        std::cerr << "Unknown poe subcommand: " << sub << "\n";
        return 1;
    }

    if (cmd == "model") {
        if (config.commandArgs.size() == 1 || config.commandArgs[1] == "help") {
            std::cout << "Usage:\n";
            std::cout << "  synapsed model status\n";
            std::cout << "  synapsed model list [--dir PATH]\n";
            std::cout << "  synapsed model load (--path PATH | --name FILENAME)\n";
            std::cout << "    [--context N] [--threads N] [--gpu-layers N] [--use-gpu 0|1] [--mmap 0|1]\n";
            std::cout << "  synapsed model unload\n";
            std::cout << "  synapsed model access get\n";
            std::cout << "  synapsed model access set --mode (PRIVATE|SHARED|PAID|COMMUNITY) [--max-slots N]\n";
            std::cout << "    [--price-per-hour-atoms N] [--price-per-request-atoms N]\n";
            std::cout << "  synapsed model remote list\n";
            std::cout << "  synapsed model remote rent --offer OFFER_ID\n";
            std::cout << "  synapsed model remote end --session SESSION_ID\n";
            return 0;
        }

        const std::string sub = config.commandArgs[1];

        if (sub == "status") {
            json out;
            std::string err;
            if (!call("model.status", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "list") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--dir" && i + 1 < config.commandArgs.size()) {
                    params["dir"] = config.commandArgs[i + 1];
                    i++;
                }
            }
            json out;
            std::string err;
            if (!call("model.list", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "load") {
            json params = json::object();
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--path" && i + 1 < config.commandArgs.size()) {
                    params["path"] = config.commandArgs[i + 1];
                    i++;
                } else if (config.commandArgs[i] == "--name" && i + 1 < config.commandArgs.size()) {
                    params["name"] = config.commandArgs[i + 1];
                    i++;
                } else if (config.commandArgs[i] == "--context" && i + 1 < config.commandArgs.size()) {
                    params["contextSize"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--threads" && i + 1 < config.commandArgs.size()) {
                    params["threads"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--gpu-layers" && i + 1 < config.commandArgs.size()) {
                    params["gpuLayers"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--use-gpu" && i + 1 < config.commandArgs.size()) {
                    params["useGpu"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                } else if (config.commandArgs[i] == "--mmap" && i + 1 < config.commandArgs.size()) {
                    params["useMmap"] = (std::stoi(config.commandArgs[i + 1]) != 0);
                    i++;
                }
            }
            json out;
            std::string err;
            if (!call("model.load", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return out.value("ok", false) ? 0 : 1;
        }

        if (sub == "unload") {
            json out;
            std::string err;
            if (!call("model.unload", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return out.value("ok", false) ? 0 : 1;
        }

        if (sub == "private" || sub == "shared" || sub == "paid" || sub == "community") {
            json params = json::object();
            params["mode"] = sub;
            json out;
            std::string err;
            if (!call("model.access.set", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "price") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed model price <pricePerHourAtoms>\n";
                return 1;
            }
            json params = json::object();
            params["pricePerHourAtoms"] = std::stoll(config.commandArgs[2]);
            json out;
            std::string err;
            if (!call("model.access.set", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "slots") {
            if (config.commandArgs.size() < 3) {
                std::cerr << "Usage: synapsed model slots <maxSlots>\n";
                return 1;
            }
            json params = json::object();
            params["maxSlots"] = std::stoll(config.commandArgs[2]);
            json out;
            std::string err;
            if (!call("model.access.set", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "access") {
            const std::string sub2 = (config.commandArgs.size() >= 3) ? config.commandArgs[2] : "help";
            if (sub2 == "get") {
                json out;
                std::string err;
                if (!call("model.access.get", json::object(), out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            if (sub2 == "set") {
                json params = json::object();
                for (size_t i = 3; i < config.commandArgs.size(); ++i) {
                    if (config.commandArgs[i] == "--mode" && i + 1 < config.commandArgs.size()) {
                        params["mode"] = config.commandArgs[i + 1];
                        i++;
                    } else if (config.commandArgs[i] == "--max-slots" && i + 1 < config.commandArgs.size()) {
                        params["maxSlots"] = std::stoll(config.commandArgs[i + 1]);
                        i++;
                    } else if (config.commandArgs[i] == "--price-per-hour-atoms" && i + 1 < config.commandArgs.size()) {
                        params["pricePerHourAtoms"] = std::stoll(config.commandArgs[i + 1]);
                        i++;
                    } else if (config.commandArgs[i] == "--price-per-request-atoms" && i + 1 < config.commandArgs.size()) {
                        params["remotePricePerRequestAtoms"] = std::stoll(config.commandArgs[i + 1]);
                        i++;
                    }
                }
                json out;
                std::string err;
                if (!call("model.access.set", params, out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            std::cerr << "Usage: synapsed model access get|set ...\n";
            return 1;
        }

        if (sub == "remote") {
            const std::string sub2 = (config.commandArgs.size() >= 3) ? config.commandArgs[2] : "help";
            if (sub2 == "list") {
                json out;
                std::string err;
                if (!call("model.remote.list", json::object(), out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            if (sub2 == "rent") {
                json params = json::object();
                for (size_t i = 3; i < config.commandArgs.size(); ++i) {
                    if (config.commandArgs[i] == "--offer" && i + 1 < config.commandArgs.size()) {
                        params["offerId"] = config.commandArgs[i + 1];
                        i++;
                    }
                }
                if (!params.contains("offerId")) {
                    std::cerr << "Usage: synapsed model remote rent --offer OFFER_ID\n";
                    return 1;
                }
                json out;
                std::string err;
                if (!call("model.remote.rent", params, out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            if (sub2 == "end") {
                json params = json::object();
                for (size_t i = 3; i < config.commandArgs.size(); ++i) {
                    if (config.commandArgs[i] == "--session" && i + 1 < config.commandArgs.size()) {
                        params["sessionId"] = config.commandArgs[i + 1];
                        i++;
                    }
                }
                if (!params.contains("sessionId")) {
                    std::cerr << "Usage: synapsed model remote end --session SESSION_ID\n";
                    return 1;
                }
                json out;
                std::string err;
                if (!call("model.remote.end", params, out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            std::cerr << "Usage: synapsed model remote list|rent|end ...\n";
            return 1;
        }

        if (sub == "market") {
            const std::string sub2 = (config.commandArgs.size() >= 3) ? config.commandArgs[2] : "help";
            if (sub2 == "listings") {
                json params = json::object();
                for (size_t i = 3; i < config.commandArgs.size(); ++i) {
                    if (config.commandArgs[i] == "--all") {
                        params["includeInactive"] = true;
                    }
                }
                json out;
                std::string err;
                if (!call("market.listings", params, out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            if (sub2 == "stats") {
                json out;
                std::string err;
                if (!call("market.stats", json::object(), out, err)) {
                    if (isRpcTransportError(err)) return std::nullopt;
                    std::cerr << "RPC failed: " << err << "\n";
                    return 1;
                }
                std::cout << out.dump(2) << "\n";
                return 0;
            }
            std::cerr << "Usage: synapsed model market listings [--all] | stats\n";
            return 1;
        }

        std::cerr << "Unknown model subcommand: " << sub << "\n";
        return 1;
    }

    if (cmd == "ai") {
        if (config.commandArgs.size() == 1 || config.commandArgs[1] == "help") {
            std::cout << "Usage:\n";
            std::cout << "  synapsed ai complete --prompt TEXT [--max-tokens N] [--temperature X] [--remote-session SESSION_ID]\n";
            std::cout << "  synapsed ai stop\n";
            return 0;
        }

        const std::string sub = config.commandArgs[1];

        if (sub == "stop") {
            json out;
            std::string err;
            if (!call("ai.stop", json::object(), out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        if (sub == "complete") {
            json params;
            for (size_t i = 2; i < config.commandArgs.size(); ++i) {
                if (config.commandArgs[i] == "--prompt" && i + 1 < config.commandArgs.size()) {
                    params["prompt"] = config.commandArgs[i + 1];
                    i++;
                } else if (config.commandArgs[i] == "--max-tokens" && i + 1 < config.commandArgs.size()) {
                    params["maxTokens"] = std::stoi(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--temperature" && i + 1 < config.commandArgs.size()) {
                    params["temperature"] = std::stod(config.commandArgs[i + 1]);
                    i++;
                } else if (config.commandArgs[i] == "--remote-session" && i + 1 < config.commandArgs.size()) {
                    params["remote"] = true;
                    params["remoteSessionId"] = config.commandArgs[i + 1];
                    i++;
                }
            }
            json out;
            std::string err;
            if (!call("ai.complete", params, out, err)) {
                if (isRpcTransportError(err)) return std::nullopt;
                std::cerr << "RPC failed: " << err << "\n";
                return 1;
            }
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        std::cerr << "Unknown ai subcommand: " << sub << "\n";
        return 1;
    }

    if (cmd == "status" || cmd == "peers" || cmd == "logs") {
        json out;
        std::string err;
        if (!call("node." + cmd, json::object(), out, err)) {
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (cmd == "seeds") {
        json out;
        std::string err;
        if (!call("node.seeds", json::object(), out, err)) {
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (cmd == "discovery") {
        json out;
        std::string err;
        if (!call("node.discovery.stats", json::object(), out, err)) {
            std::cerr << "RPC failed: " << err << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}

static bool parseDirectMethodResult(const std::string& responseBody,
                                    json& resultOut,
                                    std::string& errorOut) {
    try {
        resultOut = json::parse(responseBody);
    } catch (const std::exception& e) {
        errorOut = std::string("invalid JSON response: ") + e.what();
        return false;
    }
    return true;
}

static bool callLocalMethod(rpc::RpcCommandHandlerProvider& provider,
                            const std::string& method,
                            const json& params,
                            json& resultOut,
                            std::string& errorOut) {
    using RpcInvoker = std::string (rpc::RpcCommandHandlerProvider::*)(const std::string&);
    struct LocalCliMethodSpec {
        const char* name;
        RpcInvoker handler;
    };

    static constexpr LocalCliMethodSpec kMethods[] = {
        {"wallet.address", &rpc::RpcCommandHandlerProvider::handleRpcWalletAddress},
        {"wallet.balance", &rpc::RpcCommandHandlerProvider::handleRpcWalletBalance},
        {"naan.status", &rpc::RpcCommandHandlerProvider::handleRpcNaanStatus},
        {"naan.artifacts", &rpc::RpcCommandHandlerProvider::handleRpcNaanObservatoryArtifacts},
        {"naan.artifact", &rpc::RpcCommandHandlerProvider::handleRpcNaanObservatoryArtifactGet},
        {"naan.drafts", &rpc::RpcCommandHandlerProvider::handleRpcNaanObservatoryDrafts},
        {"naan.draft", &rpc::RpcCommandHandlerProvider::handleRpcNaanObservatoryDraftGet},
        {"naan.pipeline.dryrun", &rpc::RpcCommandHandlerProvider::handleRpcNaanPipelineDryRun},
        {"naan.pipeline.drain", &rpc::RpcCommandHandlerProvider::handleRpcNaanPipelineDrain},
        {"node.status", &rpc::RpcCommandHandlerProvider::handleRpcNodeStatus},
        {"node.peers", &rpc::RpcCommandHandlerProvider::handleRpcNodePeers},
        {"node.logs", &rpc::RpcCommandHandlerProvider::handleRpcNodeLogs},
        {"node.seeds", &rpc::RpcCommandHandlerProvider::handleRpcNodeSeeds},
        {"node.discovery.stats", &rpc::RpcCommandHandlerProvider::handleRpcNodeDiscoveryStats},
        {"node.tor.control", &rpc::RpcCommandHandlerProvider::handleRpcNodeTorControl},
        {"poe.submit", &rpc::RpcCommandHandlerProvider::handleRpcPoeSubmit},
        {"poe.submit_code", &rpc::RpcCommandHandlerProvider::handleRpcPoeSubmitCode},
        {"poe.list_code", &rpc::RpcCommandHandlerProvider::handleRpcPoeListCode},
        {"poe.fetch_code", &rpc::RpcCommandHandlerProvider::handleRpcPoeFetchCode},
        {"poe.vote", &rpc::RpcCommandHandlerProvider::handleRpcPoeVote},
        {"poe.finalize", &rpc::RpcCommandHandlerProvider::handleRpcPoeFinalize},
        {"poe.epoch", &rpc::RpcCommandHandlerProvider::handleRpcPoeEpoch},
        {"poe.export", &rpc::RpcCommandHandlerProvider::handleRpcPoeExport},
        {"poe.import", &rpc::RpcCommandHandlerProvider::handleRpcPoeImport},
        {"poe.validators", &rpc::RpcCommandHandlerProvider::handleRpcPoeValidators},
        {"model.status", &rpc::RpcCommandHandlerProvider::handleRpcModelStatus},
        {"model.list", &rpc::RpcCommandHandlerProvider::handleRpcModelList},
        {"model.load", &rpc::RpcCommandHandlerProvider::handleRpcModelLoad},
        {"model.unload", &rpc::RpcCommandHandlerProvider::handleRpcModelUnload},
        {"model.access.get", &rpc::RpcCommandHandlerProvider::handleRpcModelAccessGet},
        {"model.access.set", &rpc::RpcCommandHandlerProvider::handleRpcModelAccessSet},
        {"model.remote.list", &rpc::RpcCommandHandlerProvider::handleRpcModelRemoteList},
        {"model.remote.rent", &rpc::RpcCommandHandlerProvider::handleRpcModelRemoteRent},
        {"model.remote.end", &rpc::RpcCommandHandlerProvider::handleRpcModelRemoteEnd},
        {"market.listings", &rpc::RpcCommandHandlerProvider::handleRpcMarketListings},
        {"market.stats", &rpc::RpcCommandHandlerProvider::handleRpcMarketStats},
        {"ai.complete", &rpc::RpcCommandHandlerProvider::handleRpcAiComplete},
        {"ai.stop", &rpc::RpcCommandHandlerProvider::handleRpcAiStop},
    };

    try {
        for (const auto& spec : kMethods) {
            if (method != spec.name) continue;
            return parseDirectMethodResult((provider.*(spec.handler))(params.dump()), resultOut, errorOut);
        }
        errorOut = "unsupported_local_method: " + method;
        return false;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
}

std::optional<int> runCliViaRpc(const NodeConfig& config) {
    const uint16_t rpcPort = config.rpcPort;
    const std::string authorizationHeader = buildRpcClientAuthHeader(config);
    const CliCallFn call = [&](const std::string& method, const json& params, json& out, std::string& errOut) -> bool {
        errOut.clear();
        bool longOp = false;
        if (method.rfind("ai.", 0) == 0) longOp = true;
        if (method.rfind("poe.", 0) == 0) longOp = true;
        if (method == "model.load") longOp = true;
        if (method == "naan.pipeline.drain") longOp = true;
        if (method == "node.tor.control") longOp = true;
        int timeoutSeconds = longOp ? 300 : 3;
        return rpcCall(rpcPort, method, params, out, errOut, timeoutSeconds, authorizationHeader);
    };
    return runCliWithExecutor(config, call, isRpcTransportError);
}

int runCliLocally(const NodeConfig& config, rpc::RpcCommandHandlerProvider& provider) {
    const CliCallFn call = [&](const std::string& method, const json& params, json& out, std::string& errOut) -> bool {
        return callLocalMethod(provider, method, params, out, errOut);
    };
    auto rc = runCliWithExecutor(config, call, [](const std::string&) { return false; });
    return rc.value_or(1);
}

} // namespace synapse
