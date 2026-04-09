#include "tui/tui_command_bridge.h"

#include "crypto/crypto.h"

#include "../../third_party/llama.cpp/vendor/nlohmann/json.hpp"

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace synapse::tui {

using json = nlohmann::json;

static double atomsToNgtDouble(uint64_t atoms) {
    return static_cast<double>(atoms) / 100000000.0;
}

static std::string shortHexId(const std::string& hex) {
    return hex.size() > 8 ? hex.substr(0, 8) : hex;
}

static std::string decodeBase64Text(const std::string& encoded) {
    std::vector<uint8_t> input(encoded.begin(), encoded.end());
    auto output = crypto::base64Decode(input);
    return std::string(output.begin(), output.end());
}

static std::vector<std::string> parseCitationList(const std::string& encoded) {
    std::vector<std::string> citations;
    if (encoded.empty()) return citations;

    std::string raw = decodeBase64Text(encoded);
    for (char& c : raw) {
        if (c == ';') c = ',';
    }

    std::string current;
    auto flush = [&]() {
        std::string value = current;
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.pop_back();
        if (!value.empty()) citations.push_back(value);
    };

    for (size_t i = 0; i <= raw.size(); ++i) {
        if (i == raw.size() || raw[i] == ',') {
            flush();
            current.clear();
        } else {
            current.push_back(raw[i]);
        }
    }

    return citations;
}

void registerCoreTuiCommandHandler(TUI& ui, TuiCommandHandlerProvider& provider) {
    ui.onCommand([&provider, &ui](const std::string& cmd) {
        std::istringstream iss(cmd);
        std::string op;
        iss >> op;

        if (op == "send") {
            std::string to;
            std::string amountStr;
            iss >> to >> amountStr;
            if (to.empty() || amountStr.empty()) {
                ui.showError("Invalid send arguments");
                return;
            }

            try {
                const uint64_t amountAtoms = provider.parseTuiAmountAtoms(amountStr);
                if (amountAtoms == 0) {
                    ui.showError("Amount too small");
                    return;
                }
                (void)provider.handleTuiSendCommand(to, amountAtoms);
                ui.showMessage("Transaction submitted", Color::GREEN);
            } catch (const std::exception& e) {
                ui.showError(e.what());
            }
            return;
        }

        if (op == "poe_submit") {
            std::string q64;
            std::string a64;
            std::string s64;
            iss >> q64 >> a64 >> s64;
            if (q64.empty() || a64.empty()) {
                ui.showError("Invalid knowledge arguments");
                return;
            }

            try {
                const std::string question = decodeBase64Text(q64);
                const std::string answer = decodeBase64Text(a64);
                const std::string source = s64.empty() ? "" : decodeBase64Text(s64);
                if (question.empty() || answer.empty()) {
                    ui.showError("Question/answer empty");
                    return;
                }

                json params;
                params["question"] = question;
                params["answer"] = answer;
                params["auto_finalize"] = true;
                if (!source.empty()) {
                    params["source"] = source;
                }

                ui.updateOperationStatus("Submitting knowledge", "IN_PROGRESS", "");
                json out = json::parse(provider.handleTuiPoeSubmit(params.dump()));
                const std::string submitId = out.value("submitId", "");
                const std::string submitShort = shortHexId(submitId);
                const uint64_t expectedAtoms = out.value("expectedAcceptanceRewardAtoms", static_cast<uint64_t>(0));
                const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
                const uint32_t voteCount = out.value("voteCount", static_cast<uint32_t>(0));
                const uint32_t requiredVotes = out.value("requiredVotes", static_cast<uint32_t>(0));
                const bool finalized = out.value("finalized", false);
                const bool rewardCredited = out.value("rewardCredited", creditedAtoms > 0);

                if (finalized) {
                    ui.updateOperationStatus("Knowledge finalized", "SUCCESS", submitShort);
                    if (rewardCredited && creditedAtoms > 0) {
                        std::ostringstream details;
                        if (requiredVotes > 0) {
                            details << "Accepted by " << requiredVotes << "/" << requiredVotes << " validators";
                        }
                        ui.showRewardNotification(
                            atomsToNgtDouble(creditedAtoms),
                            "knowledge contribution",
                            submitShort,
                            details.str());
                    } else {
                        std::string msg = "Knowledge finalized (" + submitShort + "): reward pending";
                        ui.showMessage(msg, Color::GREEN);
                        ui.appendChatMessage("assistant", msg);
                    }
                } else {
                    std::ostringstream details;
                    if (requiredVotes > 0) {
                        details << voteCount << "/" << requiredVotes << " votes";
                    }
                    ui.updateOperationStatus("Validating entry", "IN_PROGRESS", details.str());

                    std::ostringstream msg;
                    msg << "Knowledge submitted (" << submitShort << ")";
                    if (requiredVotes > 0) {
                        msg << ": pending " << voteCount << "/" << requiredVotes;
                    }
                    if (expectedAtoms > 0) {
                        msg << " (+" << std::fixed << std::setprecision(8)
                            << atomsToNgtDouble(expectedAtoms) << " NGT on finalize)";
                    }
                    ui.showMessage(msg.str(), Color::GREEN);
                    ui.appendChatMessage("assistant", msg.str());
                }
            } catch (const std::exception& e) {
                ui.updateOperationStatus("Submitting knowledge", "ERROR", e.what());
                ui.showError(e.what());
            }
            return;
        }

        if (op == "poe_submit_code") {
            std::string t64;
            std::string p64;
            std::string c64;
            iss >> t64 >> p64 >> c64;
            if (t64.empty() || p64.empty()) {
                ui.showError("Invalid code arguments");
                return;
            }

            try {
                const std::string title = decodeBase64Text(t64);
                const std::string patch = decodeBase64Text(p64);
                const auto citations = parseCitationList(c64);
                if (title.empty() || patch.empty()) {
                    ui.showError("Title/patch empty");
                    return;
                }

                json params;
                params["title"] = title;
                params["patch"] = patch;
                params["auto_finalize"] = true;
                if (!citations.empty()) {
                    params["citations"] = citations;
                }

                ui.updateOperationStatus("Submitting code", "IN_PROGRESS", "");
                json out = json::parse(provider.handleTuiPoeSubmitCode(params.dump()));
                const std::string submitId = out.value("submitId", "");
                const std::string submitShort = shortHexId(submitId);
                const uint64_t expectedAtoms = out.value("expectedAcceptanceRewardAtoms", static_cast<uint64_t>(0));
                const uint64_t creditedAtoms = out.value("creditedAtoms", static_cast<uint64_t>(0));
                const uint32_t voteCount = out.value("voteCount", static_cast<uint32_t>(0));
                const uint32_t requiredVotes = out.value("requiredVotes", static_cast<uint32_t>(0));
                const bool finalized = out.value("finalized", false);
                const bool rewardCredited = out.value("rewardCredited", creditedAtoms > 0);

                if (finalized) {
                    ui.updateOperationStatus("Code finalized", "SUCCESS", submitShort);
                    if (rewardCredited && creditedAtoms > 0) {
                        std::ostringstream details;
                        if (requiredVotes > 0) {
                            details << "Accepted by " << requiredVotes << "/" << requiredVotes << " validators";
                        }
                        ui.showRewardNotification(
                            atomsToNgtDouble(creditedAtoms),
                            "code contribution",
                            submitShort,
                            details.str());
                    } else {
                        std::string msg = "Code finalized (" + submitShort + "): reward pending";
                        ui.showMessage(msg, Color::GREEN);
                        ui.appendChatMessage("assistant", msg);
                    }
                } else {
                    std::ostringstream details;
                    if (requiredVotes > 0) {
                        details << voteCount << "/" << requiredVotes << " votes";
                    }
                    ui.updateOperationStatus("Validating entry", "IN_PROGRESS", details.str());

                    std::ostringstream msg;
                    msg << "Code submitted (" << submitShort << ")";
                    if (requiredVotes > 0) {
                        msg << ": pending " << voteCount << "/" << requiredVotes;
                    }
                    if (expectedAtoms > 0) {
                        msg << " (+" << std::fixed << std::setprecision(8)
                            << atomsToNgtDouble(expectedAtoms) << " NGT on finalize)";
                    }
                    ui.showMessage(msg.str(), Color::GREEN);
                    ui.appendChatMessage("assistant", msg.str());
                }
            } catch (const std::exception& e) {
                ui.updateOperationStatus("Submitting code", "ERROR", e.what());
                ui.showError(e.what());
            }
            return;
        }

        if (op == "poe_epoch") {
            try {
                json out = json::parse(provider.handleTuiPoeEpoch("{}"));
                const uint64_t mintedAtoms = out.value("mintedAtoms", static_cast<uint64_t>(0));
                const uint64_t mintedSelfAtoms = out.value("mintedSelfAtoms", static_cast<uint64_t>(0));
                const uint64_t mintedEntries = out.value("mintedEntries", static_cast<uint64_t>(0));
                const uint64_t epochId = out.value("epochId", static_cast<uint64_t>(0));

                std::ostringstream msg;
                msg << "Epoch #" << epochId << " distributed " << std::fixed << std::setprecision(8)
                    << atomsToNgtDouble(mintedAtoms) << " NGT";
                if (mintedSelfAtoms > 0) {
                    msg << " (you: " << std::fixed << std::setprecision(8)
                        << atomsToNgtDouble(mintedSelfAtoms) << " NGT)";
                }
                msg << " to " << mintedEntries << " entries";
                ui.showMessage(msg.str(), Color::GREEN);
                ui.appendChatMessage("assistant", msg.str());
            } catch (const std::exception& e) {
                ui.showError(e.what());
            }
        }
    });
}

} // namespace synapse::tui
