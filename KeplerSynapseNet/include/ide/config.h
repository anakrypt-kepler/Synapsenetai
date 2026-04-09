#pragma once

#include <memory>
#include <string>
#include <vector>

namespace synapse {
namespace ide {

class IdeConfig {
public:
    IdeConfig();
    ~IdeConfig();

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::string getString(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
    bool getBool(const std::string& key, bool def = false) const;
    std::vector<std::string> getList(const std::string& key) const;

    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setBool(const std::string& key, bool value);
    void setList(const std::string& key, const std::vector<std::string>& values);

    std::string workingDir() const;
    void setWorkingDir(const std::string& dir);

    std::vector<std::string> contextPaths() const;
    void setContextPaths(const std::vector<std::string>& paths);

    std::vector<std::string> skillsPaths() const;
    void setSkillsPaths(const std::vector<std::string>& paths);

    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
