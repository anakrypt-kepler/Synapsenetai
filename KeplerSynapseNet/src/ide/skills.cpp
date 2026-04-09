#include "ide/skills.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace synapse {
namespace ide {

namespace {

static const std::string SkillFileName = "SKILL.md";
static const size_t MaxNameLength = 64;
static const size_t MaxDescriptionLength = 1024;
static const size_t MaxCompatibilityLength = 500;

bool splitFrontmatter(const std::string& content,
                      std::string& frontmatter,
                      std::string& body,
                      std::string& error) {
    std::string normalized = content;
    std::string::size_type pos = 0;
    while ((pos = normalized.find("\r\n", pos)) != std::string::npos) {
        normalized.replace(pos, 2, "\n");
    }
    if (normalized.substr(0, 4) != "---\n") {
        error = "no YAML frontmatter found";
        return false;
    }
    std::string rest = normalized.substr(4);
    auto endPos = rest.find("\n---");
    if (endPos == std::string::npos) {
        error = "unclosed frontmatter";
        return false;
    }
    frontmatter = rest.substr(0, endPos);
    body = rest.substr(endPos + 4);
    return true;
}

std::string extractYamlValue(const std::string& yaml, const std::string& key) {
    std::string search = key + ":";
    auto pos = yaml.find(search);
    if (pos == std::string::npos) return "";
    auto valueStart = pos + search.size();
    auto lineEnd = yaml.find('\n', valueStart);
    std::string value = (lineEnd == std::string::npos)
                            ? yaml.substr(valueStart)
                            : yaml.substr(valueStart, lineEnd - valueStart);
    auto start = value.find_first_not_of(" \t\"'");
    if (start == std::string::npos) return "";
    auto end = value.find_last_not_of(" \t\"'\r");
    return value.substr(start, end - start + 1);
}

std::string xmlEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += c;
        }
    }
    return result;
}

void walkDir(const std::string& dir, std::vector<std::string>& skillFiles,
             std::set<std::string>& seen) {
    std::string cmd = "find " + dir + " -name '" + SkillFileName + "' -type f 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return;
    char buf[4096];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);

    std::istringstream iss(output);
    std::string path;
    while (std::getline(iss, path)) {
        if (path.empty()) continue;
        if (seen.count(path)) continue;
        seen.insert(path);
        skillFiles.push_back(path);
    }
}

}

bool validateSkill(const Skill& skill, std::string& error) {
    std::vector<std::string> errs;

    if (skill.name.empty()) {
        errs.push_back("name is required");
    } else {
        if (skill.name.size() > MaxNameLength) {
            errs.push_back("name exceeds " + std::to_string(MaxNameLength) + " characters");
        }
        static const std::regex namePattern("^[a-zA-Z0-9]+(-[a-zA-Z0-9]+)*$");
        if (!std::regex_match(skill.name, namePattern)) {
            errs.push_back("name must be alphanumeric with hyphens");
        }
    }

    if (skill.description.empty()) {
        errs.push_back("description is required");
    } else if (skill.description.size() > MaxDescriptionLength) {
        errs.push_back("description exceeds " + std::to_string(MaxDescriptionLength) +
                        " characters");
    }

    if (skill.compatibility.size() > MaxCompatibilityLength) {
        errs.push_back("compatibility exceeds " + std::to_string(MaxCompatibilityLength) +
                        " characters");
    }

    if (errs.empty()) return true;
    error = errs[0];
    for (size_t i = 1; i < errs.size(); ++i) {
        error += "; " + errs[i];
    }
    return false;
}

Skill parseSkillFile(const std::string& path, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open file: " + path;
        return Skill{};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    std::string frontmatter, body;
    if (!splitFrontmatter(content, frontmatter, body, error)) {
        return Skill{};
    }

    Skill skill;
    skill.name = extractYamlValue(frontmatter, "name");
    skill.description = extractYamlValue(frontmatter, "description");
    skill.license = extractYamlValue(frontmatter, "license");
    skill.compatibility = extractYamlValue(frontmatter, "compatibility");

    while (!body.empty() && (body.front() == '\n' || body.front() == '\r' ||
                              body.front() == ' ' || body.front() == '\t')) {
        body.erase(body.begin());
    }
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' ||
                              body.back() == ' ' || body.back() == '\t')) {
        body.pop_back();
    }
    skill.instructions = body;

    auto lastSlash = path.rfind('/');
    skill.path = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : ".";
    skill.skillFilePath = path;

    return skill;
}

std::vector<Skill> discoverSkills(const std::vector<std::string>& paths) {
    std::vector<std::string> skillFiles;
    std::set<std::string> seen;

    for (const auto& base : paths) {
        walkDir(base, skillFiles, seen);
    }

    std::vector<Skill> skills;
    for (const auto& path : skillFiles) {
        std::string error;
        Skill skill = parseSkillFile(path, error);
        if (!error.empty()) continue;
        std::string valError;
        if (!validateSkill(skill, valError)) continue;
        skills.push_back(skill);
    }
    return skills;
}

std::string skillsToPromptXml(const std::vector<Skill>& skills) {
    if (skills.empty()) return "";
    std::ostringstream out;
    out << "<available_skills>\n";
    for (const auto& s : skills) {
        out << "  <skill>\n";
        out << "    <name>" << xmlEscape(s.name) << "</name>\n";
        out << "    <description>" << xmlEscape(s.description) << "</description>\n";
        out << "    <location>" << xmlEscape(s.skillFilePath) << "</location>\n";
        out << "  </skill>\n";
    }
    out << "</available_skills>";
    return out.str();
}

}
}
