#include "tui/primary_ui_spec.h"

#include <cassert>
#include <string>

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void testStatusStripHasTorOnionAndAgentActivity() {
    synapse::tui::primary_ui::ChatStatusStripInput input;
    input.torRequired = true;
    input.torReachable = false;
    input.torDegraded = true;
    input.onionServiceActive = true;
    input.clearnetFallbackAllowed = false;
    input.agentActivity = "searching";

    const std::string line = synapse::tui::primary_ui::renderChatStatusStrip(input, 200);

    assert(contains(line, "Tor:OFFLINE"));
    assert(contains(line, "Onion:DEGRADED"));
    assert(contains(line, "Clearnet:BLOCKED"));
    assert(contains(line, "Agent:SEARCHING"));
    assert(!contains(line, "Web injection"));
    assert(!contains(line, "Onion sources"));
    assert(!contains(line, "Tor for clearnet"));
}

static void testStatusStripTruncatesToWidth() {
    synapse::tui::primary_ui::ChatStatusStripInput input;
    input.torRequired = true;
    input.torReachable = true;
    input.onionServiceActive = true;
    input.clearnetFallbackAllowed = false;
    input.agentActivity = "DRAFTING";

    const std::string line = synapse::tui::primary_ui::renderChatStatusStrip(input, 18);
    assert(line.size() <= 18);
}

static void testStatusStripRedactsSecretLikeActivity() {
    synapse::tui::primary_ui::ChatStatusStripInput input;
    input.torRequired = true;
    input.torReachable = true;
    input.onionServiceActive = true;
    input.clearnetFallbackAllowed = false;
    input.agentActivity = "token=abc123";

    const std::string line = synapse::tui::primary_ui::renderChatStatusStrip(input, 200);
    assert(contains(line, "Agent:REDACTED"));
    assert(!contains(line, "ABC123"));
}

static void testStatusStripDeterministicOutput() {
    synapse::tui::primary_ui::ChatStatusStripInput input;
    input.torRequired = true;
    input.torReachable = true;
    input.onionServiceActive = false;
    input.clearnetFallbackAllowed = false;
    input.agentActivity = "idle";

    const std::string lineA = synapse::tui::primary_ui::renderChatStatusStrip(input, 120);
    const std::string lineB = synapse::tui::primary_ui::renderChatStatusStrip(input, 120);
    assert(lineA == lineB);
}

int main() {
    testStatusStripHasTorOnionAndAgentActivity();
    testStatusStripTruncatesToWidth();
    testStatusStripRedactsSecretLikeActivity();
    testStatusStripDeterministicOutput();
    return 0;
}
