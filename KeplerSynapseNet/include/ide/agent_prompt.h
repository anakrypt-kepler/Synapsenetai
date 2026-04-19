#pragma once

#include <functional>
#include <string>
#include <vector>

namespace synapse {
namespace ide {

struct ContextFile {
    std::string path;
    std::string content;
};

class AgentPrompt {
public:
    AgentPrompt();

    void setName(const std::string& name);
    void setTemplate(const std::string& tmpl);
    void setWorkingDir(const std::string& dir);
    void setPlatform(const std::string& platform);
    void setProvider(const std::string& provider);
    void setModel(const std::string& model);
    void setMaxTokenBudget(int64_t budget);

    std::string build() const;
    std::string name() const;

private:
    std::string name_;
    std::string template_;
    std::string workingDir_;
    std::string platform_;
    std::string provider_;
    std::string model_;
    int64_t maxTokenBudget_ = 0;

    bool isGitRepo() const;
    std::string getGitStatus() const;
    std::string getGitBranch() const;
    std::string getGitStatusSummary() const;
    std::string getGitRecentCommits() const;
    std::string currentDate() const;
};

}
}
