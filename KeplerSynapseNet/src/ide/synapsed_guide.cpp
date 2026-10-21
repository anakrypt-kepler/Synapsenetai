// Desk helper. A separate agent from the harvest NAAN loop.
// It talks in US English, remembers every turn on disk, and can only
// propose a change. The change runs after the user confirms it.

#include "ide/synapsed_engine.h"
#include "model/model_inference.h"
#include "../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace synapse {
namespace ide {
namespace {

constexpr size_t kPromptBudget = 3500;

const char* kSystem =
    "You are Guide, the SynapseNet desk helper. You are not the harvest agent. "
    "Speak US English. Two short sentences, then one question. Never two questions. "
    "The screen note is the live page the user is looking at. Talk about that page: "
    "what it is, what is on or off, and one next step. Use only facts from that note. "
    "Write tab names in capitals: MAIN, NAAN, SET, WALLET, SEND, HARVEST, IDE, KNOW, FIND, INTEL, MSG, NET, RENT, BLOCKS. "
    "Do not invent findings, balances, peers, or pages. Topics are not findings. "
    "If the harvest agent is stopped, it is not searching. Do not say it started. "
    "Do not quiz them about a fact already in the screen note. "
    "Never put a tab name alone on its own line. "
    "Do not change anything until they ask and then confirm. "
    "When they ask to open a tab or start or stop the harvest agent, end with one line: "
    "PROPOSE open_tab:<id> or PROPOSE naan_start or PROPOSE naan_stop. "
    "Tab ids: dashboard, wallet, transfers, blocks, knowledge, find, naan, harvest, "
    "exploits, messages, ide, network, rental, settings. "
    "If they did not ask for a change, do not write PROPOSE.";

const char* kTabs[] = {
    "dashboard", "wallet", "transfers", "blocks", "knowledge", "find", "naan",
    "harvest", "exploits", "messages", "ide", "network", "rental", "settings"
};

const char* kTabLabels[] = {
    "MAIN", "WALLET", "SEND", "BLOCKS", "KNOW", "FIND", "NAAN",
    "HARVEST", "INTEL", "MSG", "IDE", "NET", "RENT", "SET"
};

struct Turn {
    int64_t ts = 0;
    std::string role;
    std::string text;
    std::string proposalId;
    std::string action;
    std::string arg;
    std::string resolves;
};

std::string stripThink(std::string s) {
    for (;;) {
        const size_t open = s.find("<think>");
        if (open == std::string::npos) break;
        const size_t close = s.find("</think>", open);
        if (close == std::string::npos) {
            s.erase(open);
            break;
        }
        s.erase(open, close + 8 - open);
    }
    return s;
}

bool fileLooksLikeQwen3(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name.find("qwen3") != std::string::npos;
}

std::string scrub(std::string s) {
    const char* needles[] = {"<|im_start|>", "<|im_end|>", "<|endoftext|>"};
    for (const char* needle : needles) {
        const size_t n = std::strlen(needle);
        for (;;) {
            const size_t p = s.find(needle);
            if (p == std::string::npos) break;
            s.erase(p, n);
        }
    }
    return s;
}

std::string trimCopy(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string oneLine(std::string s, size_t cap) {
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    s = trimCopy(s);
    if (s.size() > cap) s.resize(cap);
    return s;
}

bool knownTab(const std::string& tab) {
    for (const char* name : kTabs) {
        if (tab == name) return true;
    }
    return false;
}

const char* tabLabel(const std::string& tab) {
    for (size_t i = 0; i < sizeof(kTabs) / sizeof(kTabs[0]); ++i) {
        if (tab == kTabs[i]) return kTabLabels[i];
    }
    return "that tab";
}

std::string leafName(const std::string& path) {
    if (path.empty()) return "";
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool cannedAssistant(const std::string& text) {
    return text.rfind("The model did not answer", 0) == 0 ||
           text.rfind("That was too much", 0) == 0 ||
           text.rfind("The GGUF is registered", 0) == 0;
}

// Live facts for the tab the user is looking at. No onion, no seed, no address.
struct PageFacts {
    std::string tab;
    std::string agent;
    std::string task;
    std::string topics;
    std::string conn;
    std::string model;
    std::string modelState;
    std::string boot;
    std::string bal;
    std::string chatModel;
    std::string agentModel;
    int peers = 0;
};

std::string agentSentence(const PageFacts& f) {
    if (f.agent == "running") {
        return "The harvest agent is running. Its task is \"" + f.task +
               "\". Topics are not findings: " + f.topics + ".";
    }
    if (f.agent == "stopping") return "The harvest agent is stopping.";
    return "The harvest agent is stopped, so it is not searching and it has no findings. "
           "Topics configured, not findings: " + f.topics + ".";
}

std::string torSentence(const PageFacts& f) {
    if (f.boot.find("100") != std::string::npos) return "Tor is up (" + f.boot + ").";
    if (f.boot.empty() || f.boot == "unknown") return "Tor bootstrap has not been reported yet.";
    return "Tor is still starting (" + f.boot + "). The mesh waits until that finishes.";
}

std::string pageRead(const PageFacts& f) {
    const std::string label = tabLabel(f.tab);
    std::ostringstream o;
    o << "You are on " << label << ". ";
    if (f.tab == "dashboard") {
        o << "MAIN is the front desk: balance, Tor, peers, the harvest agent, and the model. "
          << torSentence(f) << " Connection is " << f.conn << ". Peers: " << f.peers
          << ". Balance " << f.bal << " NGT. " << agentSentence(f)
          << " The SET model is " << f.model << " (" << f.modelState << "). "
          << "Want to open NAAN for the agent, or SET for Tor and the model?";
    } else if (f.tab == "naan") {
        o << "NAAN is the harvest agent. STATUS follows the agent: it is " << f.agent
          << ". AGENT MODEL is " << f.agentModel << ". The SET model is " << f.model
          << " (" << f.modelState << "). " << agentSentence(f)
          << " The station and crew are the room layout, not a search result. "
          << "I will not press START AGENT unless you ask and then confirm. "
          << "Want me to walk through this card?";
    } else if (f.tab == "settings") {
        o << "SET is the GGUF list, Tor, bridges, and the RAM limit. The SET model is "
          << f.model << " (" << f.modelState << "). Chat is using " << f.chatModel
          << ". " << torSentence(f) << " Harvest can run with no GGUF. Hard pages need one. "
          << "Want to keep this model, or talk about a smaller one for the chat?";
    } else if (f.tab == "wallet") {
        o << "WALLET is the private NGT balance, the stealth address, and the seed. Balance is "
          << f.bal << " NGT. I will not read the seed or the address out loud. "
          << "SEND is the tab that moves coins. Want to open SEND, or stay here?";
    } else if (f.tab == "transfers") {
        o << "SEND moves NGT to another stealth address. The memo stays on this machine. "
          << "The harvest agent does not pay this wallet. Balance is " << f.bal
          << " NGT. Want me to walk the form, or go back to WALLET?";
    } else if (f.tab == "blocks") {
        o << "BLOCKS is the proof-of-exchange chain, not hash mining. "
          << "Code NGT is minted only when other cells run the patch and agree on the same result. "
          << "Want a short read of how a block gets in, or a look at IDE?";
    } else if (f.tab == "knowledge") {
        o << "KNOW submits a local knowledge record. It stays pending, and NGT stays 0, until other cells finalize it. "
          << "Search on this page looks at local records. Want to draft a title, or look at your submissions?";
    } else if (f.tab == "find") {
        o << "FIND searches the Tor engines already built into this node. Torch is in that list. "
          << "A query goes out through Tor and comes back as titles. "
          << "This page does not start the harvest agent. "
          << torSentence(f) << " Want to run a query, or open NAAN instead?";
    } else if (f.tab == "harvest") {
        o << "HARVEST lists pages the NAAN agent saved. " << agentSentence(f)
          << " If the agent is stopped, this list does not grow. "
          << "Want to open NAAN, where the agent starts?";
    } else if (f.tab == "exploits") {
        o << "INTEL is local defensive notes on this node, each with a confidence score. "
          << "Sync reloads this node's file. It does not talk to peers, and it is not the harvest agent. "
          << "Want to know what the score means, or open HARVEST for saved pages?";
    } else if (f.tab == "messages") {
        o << "MSG writes to a live NET peer. The body is sealed to that peer and sent through Tor. "
          << "A failed hop still keeps the local log. " << torSentence(f)
          << " Want to open NET to see peers, or stay on this form?";
    } else if (f.tab == "ide") {
        o << "IDE is a patch plus tests. NGT for code is minted only when other cells run that patch and agree. "
          << "The model does not vote. Want me to explain the editor, or open BLOCKS?";
    } else if (f.tab == "network") {
        o << "NET is the peer map. The live count is " << f.peers << ". ";
        if (f.peers <= 0) o << "No cell is connected yet. A later count of 1 is this node on Tor. ";
        else if (f.peers == 1) o << "That one is this node on Tor. ";
        else o << "Dead directory onions are not in this count. ";
        o << torSentence(f) << " Messaging a peer is on MSG. Want to stay on this map, or open MSG?";
    } else if (f.tab == "rental") {
        o << "RENT is a local share switch on this build. There is no live marketplace of cards here. "
          << "It is separate from the harvest agent and from SEND. "
          << "Want a short read of what the switch does, or go back to MAIN?";
    } else {
        o << "This is the current page. " << agentSentence(f) << " " << torSentence(f)
          << " What do you want to do here?";
    }
    return o.str();
}

std::string nudgeLine(const PageFacts& f) {
    const std::string label = tabLabel(f.tab);
    if (f.tab == "naan" && f.agent == "stopped") {
        return "Still on NAAN. The agent is off, so START AGENT has not been pressed. "
               "Want me to explain the topics, or leave the agent off?";
    }
    if (f.agent == "running") {
        return "Still on " + label + ". The harvest agent is running. "
               "Want to talk about the topics it searches, without guessing what it found?";
    }
    if (f.tab == "dashboard") {
        return "Still on MAIN. I can walk the cards one by one: Tor, the harvest agent, or the model. Which one?";
    }
    return "Still on " + label + ". Ask me what the next useful click is, or what this page is for.";
}

std::string naanStartedLine(const PageFacts& f) {
    return "The harvest agent just started. It searches the configured topics through Tor and files pages on HARVEST. "
           "It does not mint NGT. Topics, not findings: " + f.topics + ". "
           "Want to talk about those topics?";
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string stripLoneTabLine(std::string text) {
    std::string cur = trimCopy(std::move(text));
    for (int pass = 0; pass < 3; ++pass) {
        if (cur.empty()) break;
        const size_t nl = cur.find_last_of("\r\n");
        std::string tail = trimCopy(nl == std::string::npos ? cur : cur.substr(nl + 1));
        while (!tail.empty()) {
            const char c = tail.back();
            if (c != '.' && c != ',' && c != '?' && c != '!' && c != ':' && c != ';') break;
            tail.pop_back();
        }
        tail = lowerCopy(trimCopy(tail));
        bool tab = false;
        for (const char* label : kTabLabels) {
            if (tail == lowerCopy(label)) {
                tab = true;
                break;
            }
        }
        if (!tab) break;
        if (nl == std::string::npos) return "";
        cur = trimCopy(cur.substr(0, nl));
    }
    return cur;
}

bool hasWord(const std::string& hay, const char* needle) {
    const std::string word = needle;
    size_t pos = 0;
    while ((pos = hay.find(word, pos)) != std::string::npos) {
        const bool left = pos == 0 || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
        const size_t end = pos + word.size();
        const bool right = end >= hay.size() || !std::isalnum(static_cast<unsigned char>(hay[end]));
        if (left && right) return true;
        pos = end;
    }
    return false;
}

struct Proposal {
    bool ok = false;
    std::string action;
    std::string arg;
};

// A small model often says "if you confirm" and forgets the PROPOSE line.
// The buttons still have to appear when the user clearly asked for a change.
Proposal inferAsked(const std::string& userText) {
    Proposal out;
    const std::string s = lowerCopy(userText);
    const bool start = hasWord(s, "start") &&
        (s.find("harvest") != std::string::npos || s.find("agent") != std::string::npos || hasWord(s, "naan"));
    const bool stop = hasWord(s, "stop") &&
        (s.find("harvest") != std::string::npos || s.find("agent") != std::string::npos || hasWord(s, "naan"));
    if (start && !stop) {
        out.ok = true;
        out.action = "naan_start";
        return out;
    }
    if (stop && !start) {
        out.ok = true;
        out.action = "naan_stop";
        return out;
    }
    const bool open = s.find("open ") != std::string::npos || s.find("go to ") != std::string::npos ||
                      s.find("switch to ") != std::string::npos || s.find("show the ") != std::string::npos ||
                      s.find("show me ") != std::string::npos;
    if (!open) return out;
    const char* words[] = {
        "wallet", "dashboard", "main", "transfers", "send", "blocks", "knowledge", "know",
        "find", "naan", "harvest", "exploits", "intel", "messages", "msg", "ide", "network", "net",
        "rental", "rent", "settings", "set"
    };
    const char* ids[] = {
        "wallet", "dashboard", "dashboard", "transfers", "transfers", "blocks", "knowledge", "knowledge",
        "find", "naan", "harvest", "exploits", "exploits", "messages", "messages", "ide", "network", "network",
        "rental", "rental", "settings", "settings"
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (!hasWord(s, words[i])) continue;
        out.ok = true;
        out.action = "open_tab";
        out.arg = ids[i];
        return out;
    }
    return out;
};

Proposal parseProposalLine(const std::string& restIn) {
    Proposal out;
    std::string rest = trimCopy(restIn);
    while (!rest.empty()) {
        const char tail = rest.back();
        if (tail != '.' && tail != ',' && tail != ';' && tail != '"' && tail != '\'') break;
        rest.pop_back();
    }
    rest = trimCopy(rest);
    if (rest.rfind("open_tab:", 0) == 0) {
        std::string tab = rest.substr(9);
        const size_t cut = tab.find_first_of(" \t");
        if (cut != std::string::npos) tab.resize(cut);
        if (!knownTab(tab)) return out;
        out.ok = true;
        out.action = "open_tab";
        out.arg = tab;
        return out;
    }
    if (rest == "naan_start" || rest == "naan_stop") {
        out.ok = true;
        out.action = rest;
    }
    return out;
}

Proposal takeProposal(const std::string& text, std::string* visible) {
    Proposal found;
    std::string kept;
    std::istringstream in(text);
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t at = line.find("PROPOSE ");
        if (at != std::string::npos) {
            Proposal next = parseProposalLine(trimCopy(line.substr(at + 8)));
            if (next.ok) found = next;
            line = trimCopy(line.substr(0, at));
            if (line.empty()) continue;
        }
        if (any) kept.push_back('\n');
        kept += line;
        any = true;
    }
    if (visible) *visible = trimCopy(kept);
    return found;
}

std::string formatTurn(const Turn& turn) {
    if (turn.text.empty()) return "";
    if (turn.role == "screen") {
        return "<|im_start|>system\nScreen note: " + turn.text +
               "\nThis is not a request. Do not write PROPOSE.\n<|im_end|>\n";
    }
    if (turn.role == "coach") {
        return "<|im_start|>user\nInstruction for you, not text to repeat: " + turn.text + "\n<|im_end|>\n";
    }
    if (turn.role == "user") {
        return "<|im_start|>user\n" + turn.text + "\n<|im_end|>\n";
    }
    if (turn.role == "assistant") {
        return "<|im_start|>assistant\n" + turn.text + "\n<|im_end|>\n";
    }
    return "";
}

std::string buildPrompt(const std::vector<Turn>& turns, bool allowPropose, size_t budget, bool skipThink) {
    std::string system = kSystem;
    if (!allowPropose) {
        system += "\nThis turn is only a screen note. Do not write a proposal line.";
    }
    int lastScreen = -1;
    int lastCoach = -1;
    for (int i = static_cast<int>(turns.size()) - 1; i >= 0; --i) {
        if (lastScreen < 0 && turns[static_cast<size_t>(i)].role == "screen") lastScreen = i;
        if (lastCoach < 0 && turns[static_cast<size_t>(i)].role == "coach") lastCoach = i;
    }
    std::vector<Turn> kept;
    kept.reserve(turns.size());
    for (int i = 0; i < static_cast<int>(turns.size()); ++i) {
        const Turn& turn = turns[static_cast<size_t>(i)];
        if (turn.role == "screen" && i != lastScreen) continue;
        if (turn.role == "coach" && i != lastCoach) continue;
        if (turn.role == "assistant" && cannedAssistant(turn.text)) continue;
        if (turn.role != "user" && turn.role != "assistant" && turn.role != "screen" && turn.role != "coach") continue;
        kept.push_back(turn);
    }
    std::vector<std::string> blocks;
    size_t used = 0;
    bool omitted = false;
    for (size_t n = kept.size(); n > 0; --n) {
        std::string block = formatTurn(kept[n - 1]);
        if (block.empty()) continue;
        if (used + block.size() > budget && !blocks.empty()) {
            omitted = true;
            break;
        }
        if (block.size() > budget) {
            block = block.substr(block.size() - budget);
            omitted = true;
        }
        used += block.size();
        blocks.push_back(std::move(block));
    }
    std::reverse(blocks.begin(), blocks.end());
    std::string out;
    out.reserve(system.size() + used + 200);
    out += "<|im_start|>system\n";
    out += system;
    if (omitted) {
        out += "\nOlder turns are saved in full on disk. This call rereads only the newest ones.";
    }
    out += "\n<|im_end|>\n";
    for (const auto& block : blocks) out += block;
    out += "<|im_start|>assistant\n";
    // Qwen3 spends the short reply budget inside a thinking trace unless this
    // empty trace is already closed.
    if (skipThink) out += "<think>\n\n</think>\n";
    return out;
}

int64_t guideNow() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

std::string SynapsedEngine::guideRpc(const std::string& op, const std::string& paramsJson) {
    nlohmann::json p = nlohmann::json::parse(paramsJson.empty() ? "{}" : paramsJson, nullptr, false);
    if (p.is_discarded() || !p.is_object()) p = nlohmann::json::object();

    const std::string path = dataDir_ + "/guide_chat.jsonl";

    auto readTurns = [&]() -> std::vector<Turn> {
        std::vector<Turn> turns;
        std::ifstream in(path);
        if (!in.good()) return turns;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            nlohmann::json row = nlohmann::json::parse(line, nullptr, false);
            if (row.is_discarded() || !row.is_object()) continue;
            Turn turn;
            if (row.contains("ts") && row["ts"].is_number()) turn.ts = row["ts"].get<int64_t>();
            if (row.contains("role") && row["role"].is_string()) turn.role = row["role"].get<std::string>();
            if (row.contains("text") && row["text"].is_string()) turn.text = row["text"].get<std::string>();
            if (row.contains("resolves") && row["resolves"].is_string())
                turn.resolves = row["resolves"].get<std::string>();
            if (row.contains("proposal") && row["proposal"].is_object()) {
                const auto& prop = row["proposal"];
                if (prop.contains("id") && prop["id"].is_string()) turn.proposalId = prop["id"].get<std::string>();
                if (prop.contains("action") && prop["action"].is_string())
                    turn.action = prop["action"].get<std::string>();
                if (prop.contains("arg") && prop["arg"].is_string()) turn.arg = prop["arg"].get<std::string>();
            }
            turns.push_back(std::move(turn));
        }
        return turns;
    };

    auto restorePending = [&](const std::vector<Turn>& turns) {
        guidePendingId_.clear();
        guidePendingAction_.clear();
        guidePendingArg_.clear();
        std::string openId;
        std::string openAction;
        std::string openArg;
        for (const auto& turn : turns) {
            if (!turn.proposalId.empty() && !turn.action.empty()) {
                openId = turn.proposalId;
                openAction = turn.action;
                openArg = turn.arg;
            }
            if (!turn.resolves.empty() && turn.resolves == openId) {
                openId.clear();
                openAction.clear();
                openArg.clear();
            }
        }
        guidePendingId_ = openId;
        guidePendingAction_ = openAction;
        guidePendingArg_ = openArg;
    };

    auto appendTurn = [&](const Turn& turn) {
        nlohmann::json row;
        row["ts"] = turn.ts == 0 ? guideNow() : turn.ts;
        row["role"] = turn.role;
        row["text"] = turn.text;
        if (!turn.resolves.empty()) row["resolves"] = turn.resolves;
        if (!turn.proposalId.empty()) {
            row["proposal"] = {
                {"id", turn.proposalId},
                {"action", turn.action},
                {"arg", turn.arg}
            };
        }
        std::ofstream out(path, std::ios::app);
        if (!out.good()) return false;
        out << row.dump() << "\n";
        return true;
    };

    auto pendingJson = [&]() -> nlohmann::json {
        if (guidePendingId_.empty()) return nullptr;
        return nlohmann::json{
            {"id", guidePendingId_},
            {"action", guidePendingAction_},
            {"arg", guidePendingArg_}
        };
    };

    if (op == "history") {
        std::lock_guard<std::mutex> lock(guideMtx_);
        const std::vector<Turn> turns = readTurns();
        restorePending(turns);
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& turn : turns) {
            if (turn.role != "user" && turn.role != "assistant") continue;
            if (turn.text.empty()) continue;
            messages.push_back({{"role", turn.role}, {"text", turn.text}, {"ts", turn.ts}});
        }
        nlohmann::json out;
        out["ok"] = true;
        out["agent_id"] = "guide";
        out["messages"] = std::move(messages);
        out["pending"] = pendingJson();
        return out.dump();
    }

    if (op == "clear") {
        if (guideBusy_.load()) return "{\"error\":\"busy\"}";
        std::lock_guard<std::mutex> lock(guideMtx_);
        std::ofstream wiped(path, std::ios::trunc);
        if (!wiped.good()) return "{\"error\":\"guide log unavailable\"}";
        guidePendingId_.clear();
        guidePendingAction_.clear();
        guidePendingArg_.clear();
        return "{\"ok\":true,\"agent_id\":\"guide\"}";
    }

    if (op == "confirm" || op == "refuse") {
        std::string id;
        if (p.contains("id") && p["id"].is_string()) id = p["id"].get<std::string>();
        std::string action;
        std::string arg;
        {
            std::lock_guard<std::mutex> lock(guideMtx_);
            if (guidePendingId_.empty()) {
                const std::vector<Turn> turns = readTurns();
                restorePending(turns);
            }
            if (id.empty() || id != guidePendingId_ || guidePendingAction_.empty()) {
                return "{\"error\":\"proposal_gone\"}";
            }
            action = guidePendingAction_;
            arg = guidePendingArg_;
            guidePendingId_.clear();
            guidePendingAction_.clear();
            guidePendingArg_.clear();
        }
        std::string text;
        if (op == "refuse") {
            text = "You refused that. I left it alone. What do you want to do instead?";
        } else if (action == "naan_start") {
            startNaan();
            text = "Confirmed. Starting the harvest agent. Want to talk about what it searches?";
        } else if (action == "naan_stop") {
            stopNaan();
            text = "Confirmed. Stopping the harvest agent. What should we look at next?";
        } else if (action == "open_tab") {
            text = std::string("Confirmed. Opening ") + tabLabel(arg) + ". Want a short tour of this tab?";
        } else {
            return "{\"error\":\"proposal_gone\"}";
        }
        {
            std::lock_guard<std::mutex> lock(guideMtx_);
            Turn turn;
            turn.ts = guideNow();
            turn.role = "assistant";
            turn.text = text;
            turn.resolves = id;
            appendTurn(turn);
        }
        nlohmann::json out;
        out["ok"] = true;
        out["text"] = text;
        out["action"] = op == "confirm" ? action : "";
        out["arg"] = op == "confirm" ? arg : "";
        return out.dump();
    }

    if (op != "say" && op != "observe") return "{\"error\":\"unknown guide op\"}";

    bool expected = false;
    if (!guideBusy_.compare_exchange_strong(expected, true)) {
        return "{\"error\":\"busy\"}";
    }
    struct BusyGuard {
        std::atomic<bool>& flag;
        ~BusyGuard() { flag.store(false); }
    } guard{guideBusy_};

    const bool observe = op == "observe";
    std::string userText;
    if (!observe) {
        if (p.contains("text") && p["text"].is_string()) userText = scrub(p["text"].get<std::string>());
        userText = trimCopy(userText);
        if (userText.empty()) return "{\"error\":\"empty message\"}";
    }

    std::string event = "hello";
    if (p.contains("event") && p["event"].is_string()) event = oneLine(scrub(p["event"].get<std::string>()), 40);
    if (event != "hello" && event != "checkin" && event != "nudge" &&
        event != "naan_started" && event != "page") event = "hello";
    std::string tab = "dashboard";
    if (p.contains("screen") && p["screen"].is_object() &&
        p["screen"].contains("tab") && p["screen"]["tab"].is_string()) {
        const std::string asked = p["screen"]["tab"].get<std::string>();
        if (knownTab(asked)) tab = asked;
    }

    PageFacts facts;
    facts.tab = tab;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        facts.task = oneLine(naanCurrentTask_.empty() ? "none" : naanCurrentTask_, 160);
        for (size_t i = 0; i < cfgTopics_.size(); ++i) {
            if (i) facts.topics += ", ";
            facts.topics += cfgTopics_[i];
        }
        if (facts.topics.empty()) facts.topics = "none";
        facts.topics = oneLine(facts.topics, 120);
        facts.conn = oneLine(connectionType_.empty() ? "unknown" : connectionType_, 24);
        facts.model = oneLine(modelLoaded_ ? modelName_ : "none", 80);
        if (facts.model.empty()) facts.model = "none";
        if (inferenceReady_) facts.modelState = "in RAM";
        else if (modelLoaded_) facts.modelState = "registered, not in RAM yet";
        else facts.modelState = "not loaded";
        facts.boot = oneLine(torBootstrap_.empty() ? "unknown" : torBootstrap_, 40);
        facts.bal = oneLine(balance_.empty() ? "unknown" : balance_, 24);
        facts.peers = peerCount_;
        const std::string naanState = naanState_;
        if (naanState == "active") facts.agent = "running";
        else if (naanState == "stopping") facts.agent = "stopping";
        else if (!naanState.empty() && naanState != "off") facts.agent = naanState;
        else facts.agent = "stopped";
        facts.chatModel = guideModelPath_.empty() ? "the SET model" : leafName(guideModelPath_);
        facts.agentModel = naanModelPath_.empty() ? "the same file as SET" : leafName(naanModelPath_);
    }
    const std::string brief = pageRead(facts);
    // Opening the app, changing tabs, and quiet nudges are a read of this page.
    // The GGUF is reserved for a message the user actually typed.
    if (observe) {
        const std::string spoken = event == "nudge" ? nudgeLine(facts)
            : event == "naan_started" ? naanStartedLine(facts)
            : brief;
        {
            std::lock_guard<std::mutex> lock(guideMtx_);
            Turn reply;
            reply.ts = guideNow();
            reply.role = "assistant";
            reply.text = spoken;
            if (!appendTurn(reply)) return "{\"error\":\"guide log unavailable\"}";
        }
        nlohmann::json out;
        out["ok"] = true;
        out["agent_id"] = "guide";
        out["text"] = spoken;
        out["proposal"] = nullptr;
        return out.dump();
    }
    const std::string coach =
        "The screen note is the live page. Answer the user's last message about that page. "
        "Two short US English sentences, then one question. Use only facts from the screen note. "
        "Do not invent findings, balances, or pages. "
        "If they asked to open a tab or start or stop the harvest agent, "
        "ask them to confirm and add one PROPOSE line. Otherwise do not write PROPOSE.";
    const std::string agent = facts.agent;

    std::vector<Turn> turns;
    {
        std::lock_guard<std::mutex> lock(guideMtx_);
        turns = readTurns();
        restorePending(turns);
        if (!guidePendingId_.empty()) {
            Turn drop;
            drop.ts = guideNow();
            drop.role = "decision";
            drop.resolves = guidePendingId_;
            appendTurn(drop);
            guidePendingId_.clear();
            guidePendingAction_.clear();
            guidePendingArg_.clear();
        }
        Turn note;
        note.ts = guideNow();
        note.role = "screen";
        note.text = brief;
        if (!appendTurn(note)) return "{\"error\":\"guide log unavailable\"}";
        turns.push_back(note);
        Turn fresh;
        fresh.ts = note.ts;
        fresh.role = "user";
        fresh.text = userText;
        if (!appendTurn(fresh)) return "{\"error\":\"guide log unavailable\"}";
        turns.push_back(fresh);
        if (!coach.empty()) {
            Turn hint;
            hint.ts = fresh.ts;
            hint.role = "coach";
            hint.text = coach;
            if (!appendTurn(hint)) return "{\"error\":\"guide log unavailable\"}";
            turns.push_back(hint);
        }
    }

    std::string guideFile;
    std::string guideRole = "desktop";
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!guideModelPath_.empty()) {
            guideFile = guideModelPath_;
            if (guideModelPath_ != modelPath_) guideRole = "guide";
        } else {
            guideFile = modelPath_;
        }
    }
    const bool skipThink = fileLooksLikeQwen3(guideFile);
    if (!ensureRoleModel(guideRole, guideFile)) {
        std::lock_guard<std::mutex> lock(guideMtx_);
        Turn fail;
        fail.ts = guideNow();
        fail.role = "assistant";
        fail.text = "That model did not load. Pick another one above, or use the SET model.";
        appendTurn(fail);
        nlohmann::json out;
        out["ok"] = false;
        out["error"] = fail.text;
        out["text"] = fail.text;
        return out.dump();
    }

    std::map<std::string, float> genParams;
    genParams["max_tokens"] = 160.f;
    genParams["temperature"] = 0.4f;
    // 14B on CPU can spend the old 120s budget inside prompt eval and return nothing.
    genParams["timeout_ms"] = 240000.f;
    auto runOnce = [&](size_t budget) -> std::string {
        const std::string prompt = buildPrompt(turns, !observe, budget, skipThink);
        std::lock_guard<std::mutex> llama(llamaMtx_);
        if (!llamaEngine_) return "";
        return llamaEngine_->runSyncText(guideRole, prompt, genParams);
    };
    std::string raw = stripThink(scrub(runOnce(kPromptBudget)));
    if (raw.rfind("[Decode failed]", 0) == 0 || raw.rfind("[Tokenization", 0) == 0) {
        raw = stripThink(scrub(runOnce(1200)));
    }
    std::string visible;
    Proposal proposal = takeProposal(raw, &visible);
    visible = stripLoneTabLine(visible);
    if (!proposal.ok) proposal = inferAsked(userText);
    if (visible.empty() && proposal.ok) {
        visible = "I can do that if you confirm.";
    }
    if (visible.empty() || visible == "empty model output" || visible.rfind("Model not loaded", 0) == 0 ||
        visible.rfind("[Decode failed]", 0) == 0 || visible.rfind("[Tokenization", 0) == 0) {
        visible = brief;
        proposal = inferAsked(userText);
    }
    const bool canned = visible.rfind("The model did not answer", 0) == 0 ||
                        visible.rfind("That was too much", 0) == 0 ||
                        visible.rfind("The GGUF is registered", 0) == 0;
    if (!canned && !proposal.ok && visible.find('?') == std::string::npos) {
        if (agent == "running") visible += " Want to talk about what it searches?";
        else if (agent == "stopped") visible += " Want me to point you at NAAN?";
        else visible += " What should we look at next?";
    }

    static std::atomic<uint32_t> seq{0};
    std::string proposalId;
    {
        std::lock_guard<std::mutex> lock(guideMtx_);
        Turn reply;
        reply.ts = guideNow();
        reply.role = "assistant";
        reply.text = visible;
        if (proposal.ok) {
            proposalId = "g" + std::to_string(reply.ts) + "-" + std::to_string(seq.fetch_add(1));
            reply.proposalId = proposalId;
            reply.action = proposal.action;
            reply.arg = proposal.arg;
            guidePendingId_ = proposalId;
            guidePendingAction_ = proposal.action;
            guidePendingArg_ = proposal.arg;
        }
        appendTurn(reply);
    }

    nlohmann::json out;
    out["ok"] = true;
    out["agent_id"] = "guide";
    out["text"] = visible;
    if (proposal.ok) {
        out["proposal"] = {{"id", proposalId}, {"action", proposal.action}, {"arg", proposal.arg}};
    } else {
        out["proposal"] = nullptr;
    }
    return out.dump();
}

} // namespace ide
} // namespace synapse
