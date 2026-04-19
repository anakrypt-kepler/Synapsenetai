#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace synapse {
namespace ide {

struct Skill {
    std::string name;
    std::string description;
    std::string license;
    std::string compatibility;
    std::map<std::string, std::string> metadata;
    std::string instructions;
    std::string path;
    std::string skillFilePath;
};

bool validateSkill(const Skill& skill, std::string& error);

Skill parseSkillFile(const std::string& path, std::string& error);

std::vector<Skill> discoverSkills(const std::vector<std::string>& paths);

std::string skillsToPromptXml(const std::vector<Skill>& skills);

}
}
