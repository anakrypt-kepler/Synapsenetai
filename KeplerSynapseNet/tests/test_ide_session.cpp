#include "ide/config.h"
#include "ide/patch.h"
#include "ide/session.h"
#include "ide/session_db.h"
#include "ide/skills.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <iostream>
#include <string>

static std::string tmpDir;

static void setupTmpDir() {
    char tmpl[] = "/tmp/ide_session_test_XXXXXX";
    char* dir = mkdtemp(tmpl);
    assert(dir != nullptr);
    tmpDir = dir;
}

static void cleanupTmpDir() {
    std::string cmd = "rm -rf " + tmpDir;
    std::system(cmd.c_str());
}

static bool testCreateAndLoadSession() {
    synapse::ide::SessionService svc;

    auto s1 = svc.create("Test Session");
    assert(!s1.id.empty());
    assert(s1.title == "Test Session");
    assert(s1.createdAt > 0);

    auto loaded = svc.get(s1.id);
    assert(loaded.id == s1.id);
    assert(loaded.title == s1.title);

    std::cerr << "  testCreateAndLoadSession passed\n";
    return true;
}

static bool testSessionList() {
    synapse::ide::SessionService svc;

    svc.create("Session A");
    svc.create("Session B");
    svc.create("Session C");

    auto sessions = svc.list();
    assert(sessions.size() == 3);

    std::cerr << "  testSessionList passed\n";
    return true;
}

static bool testSessionMessageHistory() {
    synapse::ide::SessionService svc;
    auto session = svc.create("Chat Session");

    synapse::ide::SessionMessage msg1;
    msg1.role = "user";
    msg1.content = "Hello";
    svc.addMessage(session.id, msg1);

    synapse::ide::SessionMessage msg2;
    msg2.role = "assistant";
    msg2.content = "Hi there!";
    svc.addMessage(session.id, msg2);

    auto messages = svc.getMessages(session.id);
    assert(messages.size() == 2);
    assert(messages[0].role == "user");
    assert(messages[0].content == "Hello");
    assert(messages[1].role == "assistant");
    assert(messages[1].content == "Hi there!");

    auto updated = svc.get(session.id);
    assert(updated.messageCount == 2);

    std::cerr << "  testSessionMessageHistory passed\n";
    return true;
}

static bool testSessionSaveAndDelete() {
    synapse::ide::SessionService svc;
    auto session = svc.create("Mutable Session");

    session.title = "Updated Title";
    session.promptTokens = 100;
    session.completionTokens = 200;
    session.cost = 0.05;
    assert(svc.save(session));

    auto loaded = svc.get(session.id);
    assert(loaded.title == "Updated Title");
    assert(loaded.promptTokens == 100);

    assert(svc.remove(session.id));
    auto deleted = svc.get(session.id);
    assert(deleted.id.empty());

    std::cerr << "  testSessionSaveAndDelete passed\n";
    return true;
}

static bool testSessionThreads() {
    synapse::ide::SessionService svc;
    auto parent = svc.create("Parent");
    auto child = svc.createThread(parent.id, "Child Thread");

    assert(child.parentSessionId == parent.id);
    assert(child.title == "Child Thread");

    std::cerr << "  testSessionThreads passed\n";
    return true;
}

static bool testAgentToolSessionId() {
    auto combined = synapse::ide::SessionService::createAgentToolSessionId("msg123", "tool456");
    assert(combined == "msg123$$tool456");

    std::string msgId, toolId;
    assert(synapse::ide::SessionService::parseAgentToolSessionId(combined, msgId, toolId));
    assert(msgId == "msg123");
    assert(toolId == "tool456");

    assert(synapse::ide::SessionService::isAgentToolSession(combined));
    assert(!synapse::ide::SessionService::isAgentToolSession("normal-session-id"));

    std::string m, t;
    assert(!synapse::ide::SessionService::parseAgentToolSessionId("no-separator", m, t));

    std::cerr << "  testAgentToolSessionId passed\n";
    return true;
}

static bool testPatchParse() {
    std::string diff =
        "--- a/file.txt\n"
        "+++ b/file.txt\n"
        "@@ -1,3 +1,3 @@\n"
        " line1\n"
        "-line2\n"
        "+LINE2\n"
        " line3\n";

    std::string error;
    auto patches = synapse::ide::parseUnifiedDiff(diff, error);
    assert(error.empty());
    assert(patches.size() == 1);
    assert(patches[0].oldPath == "file.txt");
    assert(patches[0].newPath == "file.txt");
    assert(patches[0].hunks.size() == 1);
    assert(patches[0].hunks[0].oldStart == 1);
    assert(patches[0].hunks[0].lines.size() == 4);

    std::cerr << "  testPatchParse passed\n";
    return true;
}

static bool testPatchApply() {
    std::string oldContent = "line1\nline2\nline3\n";

    synapse::ide::FilePatch patch;
    patch.oldPath = "file.txt";
    patch.newPath = "file.txt";
    synapse::ide::Hunk h;
    h.oldStart = 1;
    h.oldCount = 3;
    h.newStart = 1;
    h.newCount = 3;
    h.lines.push_back({synapse::ide::LineKind::Context, "line1"});
    h.lines.push_back({synapse::ide::LineKind::Delete, "line2"});
    h.lines.push_back({synapse::ide::LineKind::Add, "LINE2"});
    h.lines.push_back({synapse::ide::LineKind::Context, "line3"});
    patch.hunks.push_back(h);

    std::string error;
    std::string result = synapse::ide::applyPatch(oldContent, patch, error);
    assert(error.empty());
    assert(result == "line1\nLINE2\nline3\n");

    std::cerr << "  testPatchApply passed\n";
    return true;
}

static bool testPatchParseAndApply() {
    std::string diff =
        "--- a/test.txt\n"
        "+++ b/test.txt\n"
        "@@ -1,3 +1,4 @@\n"
        " alpha\n"
        "-beta\n"
        "+BETA\n"
        "+gamma_new\n"
        " gamma\n";

    std::string error;
    auto patches = synapse::ide::parseUnifiedDiff(diff, error);
    assert(error.empty());
    assert(patches.size() == 1);

    std::string oldContent = "alpha\nbeta\ngamma\n";
    std::string result = synapse::ide::applyPatch(oldContent, patches[0], error);
    assert(error.empty());
    assert(result == "alpha\nBETA\ngamma_new\ngamma\n");

    std::cerr << "  testPatchParseAndApply passed\n";
    return true;
}

static bool testSkillValidation() {
    synapse::ide::Skill valid;
    valid.name = "test-skill";
    valid.description = "A test skill";
    std::string error;
    assert(synapse::ide::validateSkill(valid, error));

    synapse::ide::Skill noName;
    noName.description = "Has desc";
    assert(!synapse::ide::validateSkill(noName, error));
    assert(error.find("name") != std::string::npos);

    synapse::ide::Skill noDesc;
    noDesc.name = "valid-name";
    assert(!synapse::ide::validateSkill(noDesc, error));
    assert(error.find("description") != std::string::npos);

    synapse::ide::Skill badName;
    badName.name = "invalid name!";
    badName.description = "Some desc";
    assert(!synapse::ide::validateSkill(badName, error));

    std::cerr << "  testSkillValidation passed\n";
    return true;
}

static bool testSkillParsing() {
    std::string skillPath = tmpDir + "/my-skill/SKILL.md";
    std::string skillDir = tmpDir + "/my-skill";
    std::string mkdirCmd = "mkdir -p " + skillDir;
    std::system(mkdirCmd.c_str());

    {
        std::ofstream f(skillPath);
        f << "---\n"
          << "name: my-skill\n"
          << "description: A cool skill\n"
          << "license: MIT\n"
          << "---\n"
          << "Instructions go here.\n";
    }

    std::string error;
    auto skill = synapse::ide::parseSkillFile(skillPath, error);
    assert(error.empty());
    assert(skill.name == "my-skill");
    assert(skill.description == "A cool skill");
    assert(skill.license == "MIT");
    assert(skill.instructions.find("Instructions") != std::string::npos);
    assert(skill.skillFilePath == skillPath);

    std::cerr << "  testSkillParsing passed\n";
    return true;
}

static bool testSkillDiscovery() {
    std::string skillDir1 = tmpDir + "/skills/tool-a";
    std::string skillDir2 = tmpDir + "/skills/tool-b";
    std::system(("mkdir -p " + skillDir1).c_str());
    std::system(("mkdir -p " + skillDir2).c_str());

    {
        std::ofstream f(skillDir1 + "/SKILL.md");
        f << "---\nname: tool-a\ndescription: Tool A\n---\nInstructions A\n";
    }
    {
        std::ofstream f(skillDir2 + "/SKILL.md");
        f << "---\nname: tool-b\ndescription: Tool B\n---\nInstructions B\n";
    }

    auto skills = synapse::ide::discoverSkills({tmpDir + "/skills"});
    assert(skills.size() == 2);

    std::cerr << "  testSkillDiscovery passed\n";
    return true;
}

static bool testSkillsToXml() {
    std::vector<synapse::ide::Skill> skills;
    synapse::ide::Skill s1;
    s1.name = "test-skill";
    s1.description = "A <test> skill";
    s1.skillFilePath = "/path/to/SKILL.md";
    skills.push_back(s1);

    std::string xml = synapse::ide::skillsToPromptXml(skills);
    assert(xml.find("<available_skills>") != std::string::npos);
    assert(xml.find("<name>test-skill</name>") != std::string::npos);
    assert(xml.find("&lt;test&gt;") != std::string::npos);

    std::string empty = synapse::ide::skillsToPromptXml({});
    assert(empty.empty());

    std::cerr << "  testSkillsToXml passed\n";
    return true;
}

static bool testIdeConfig() {
    synapse::ide::IdeConfig config;

    config.setString("model.name", "claude-3");
    assert(config.getString("model.name") == "claude-3");
    assert(config.getString("nonexistent", "default") == "default");

    config.setInt("model.context_size", 4096);
    assert(config.getInt("model.context_size") == 4096);

    config.setBool("ide.auto_save", true);
    assert(config.getBool("ide.auto_save") == true);

    config.setList("ide.paths", {"path1", "path2", "path3"});
    auto paths = config.getList("ide.paths");
    assert(paths.size() == 3);
    assert(paths[0] == "path1");

    config.setWorkingDir("/tmp/test");
    assert(config.workingDir() == "/tmp/test");

    std::cerr << "  testIdeConfig passed\n";
    return true;
}

static bool testIdeConfigSaveLoad() {
    std::string configPath = tmpDir + "/test_config.ini";

    {
        synapse::ide::IdeConfig config;
        config.setString("ide.theme", "dark");
        config.setInt("ide.tab_size", 4);
        config.setBool("ide.word_wrap", true);
        assert(config.save(configPath));
    }

    {
        synapse::ide::IdeConfig config;
        assert(config.load(configPath));
        assert(config.getString("ide.theme") == "dark");
        assert(config.getInt("ide.tab_size") == 4);
        assert(config.getBool("ide.word_wrap") == true);
    }

    std::cerr << "  testIdeConfigSaveLoad passed\n";
    return true;
}

static bool testIdeConfigReset() {
    synapse::ide::IdeConfig config;
    config.setString("key", "value");
    assert(config.getString("key") == "value");

    config.reset();
    assert(config.getString("key") == "");

    std::cerr << "  testIdeConfigReset passed\n";
    return true;
}

static bool testSessionDBInMemory() {
    synapse::ide::SessionDB db;
    assert(db.open(":memory:"));
    assert(db.isOpen());

    synapse::ide::Session s;
    s.id = "test-session-1";
    s.title = "Test Session";
    s.createdAt = 1000;
    s.updatedAt = 1000;
    assert(db.createSession(s));

    auto loaded = db.getSession("test-session-1");
    assert(loaded.id == "test-session-1");
    assert(loaded.title == "Test Session");

    s.title = "Updated";
    s.updatedAt = 2000;
    assert(db.updateSession(s));
    loaded = db.getSession("test-session-1");
    assert(loaded.title == "Updated");

    synapse::ide::SessionMessage msg;
    msg.id = "msg-1";
    msg.sessionId = "test-session-1";
    msg.role = "user";
    msg.content = "Hello";
    msg.createdAt = 1500;
    assert(db.addMessage(msg));

    auto messages = db.getMessages("test-session-1");
    assert(messages.size() == 1);
    assert(messages[0].content == "Hello");

    assert(db.messageCount("test-session-1") == 1);

    assert(db.deleteSession("test-session-1"));
    loaded = db.getSession("test-session-1");
    assert(loaded.id.empty());

    db.close();
    assert(!db.isOpen());

    std::cerr << "  testSessionDBInMemory passed\n";
    return true;
}

static bool testSessionDBList() {
    synapse::ide::SessionDB db;
    assert(db.open(":memory:"));

    for (int i = 0; i < 5; ++i) {
        synapse::ide::Session s;
        s.id = "session-" + std::to_string(i);
        s.title = "Session " + std::to_string(i);
        s.createdAt = 1000 + i;
        s.updatedAt = 1000 + i;
        db.createSession(s);
    }

    auto sessions = db.listSessions();
    assert(sessions.size() == 5);

    db.close();

    std::cerr << "  testSessionDBList passed\n";
    return true;
}

int main() {
    setupTmpDir();

    std::cerr << "Running IDE Session tests...\n";

    int failures = 0;
    if (!testCreateAndLoadSession()) ++failures;
    if (!testSessionList()) ++failures;
    if (!testSessionMessageHistory()) ++failures;
    if (!testSessionSaveAndDelete()) ++failures;
    if (!testSessionThreads()) ++failures;
    if (!testAgentToolSessionId()) ++failures;
    if (!testPatchParse()) ++failures;
    if (!testPatchApply()) ++failures;
    if (!testPatchParseAndApply()) ++failures;
    if (!testSkillValidation()) ++failures;
    if (!testSkillParsing()) ++failures;
    if (!testSkillDiscovery()) ++failures;
    if (!testSkillsToXml()) ++failures;
    if (!testIdeConfig()) ++failures;
    if (!testIdeConfigSaveLoad()) ++failures;
    if (!testIdeConfigReset()) ++failures;
    if (!testSessionDBInMemory()) ++failures;
    if (!testSessionDBList()) ++failures;

    cleanupTmpDir();

    if (failures == 0) {
        std::cerr << "All IDE Session tests passed!\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed!\n";
    return 1;
}
