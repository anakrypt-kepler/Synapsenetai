#pragma once

#include <string>
#include <vector>

namespace synapse {
namespace ide {

enum class LineKind {
    Context,
    Add,
    Delete
};

struct HunkLine {
    LineKind kind = LineKind::Context;
    std::string text;
};

struct Hunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    std::vector<HunkLine> lines;
};

struct FilePatch {
    std::string oldPath;
    std::string newPath;
    std::vector<Hunk> hunks;
};

std::vector<FilePatch> parseUnifiedDiff(const std::string& input, std::string& error);

std::string applyPatch(const std::string& oldContent, const FilePatch& patch,
                       std::string& error);

}
}
