#include "ide/patch.h"

#include <sstream>

namespace synapse {
namespace ide {

namespace {

std::vector<std::string> splitLines(const std::string& s) {
    if (s.empty()) return {};
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        result.push_back(line);
    }
    return result;
}

std::vector<std::string> splitContentLines(const std::string& content) {
    if (content.empty()) return {};
    std::string c = content;
    if (!c.empty() && c.back() == '\n') c.pop_back();
    if (c.empty()) return {};
    return splitLines(c);
}

std::string extractDiffBlock(const std::string& input) {
    std::string trimmed = input;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\n' ||
                                 trimmed.front() == '\r' || trimmed.front() == '\t')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n' ||
                                 trimmed.back() == '\r' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) return "";

    auto lines = splitLines(trimmed);
    std::string best;

    bool inFence = false;
    int fenceStart = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string l = lines[i];
        auto pos = l.find_first_not_of(" \t");
        if (pos != std::string::npos) l = l.substr(pos);
        if (l.substr(0, 3) == "```") {
            if (!inFence) {
                inFence = true;
                fenceStart = static_cast<int>(i) + 1;
                continue;
            }
            inFence = false;
            if (fenceStart >= 0 && fenceStart < static_cast<int>(i)) {
                std::string block;
                for (int j = fenceStart; j < static_cast<int>(i); ++j) {
                    if (!block.empty()) block += "\n";
                    block += lines[j];
                }
                if (block.find("diff --git ") != std::string::npos ||
                    (block.find("--- ") != std::string::npos &&
                     block.find("+++ ") != std::string::npos)) {
                    return block;
                }
                if (best.empty()) best = block;
            }
            fenceStart = -1;
        }
    }

    if (!best.empty()) return best;
    return trimmed;
}

std::string normalizePath(const std::string& p) {
    std::string result = p;
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    if (result.empty()) return result;
    if (result == "/dev/null") return result;
    if (result.size() > 2 && result[0] == 'a' && result[1] == '/') result = result.substr(2);
    else if (result.size() > 2 && result[0] == 'b' && result[1] == '/') result = result.substr(2);
    return result;
}

void parseDiffGitLine(const std::string& line, std::string& oldPath, std::string& newPath) {
    std::string rest = line.substr(11);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.erase(rest.begin());
    }
    auto spacePos = rest.find(' ');
    if (spacePos != std::string::npos) {
        oldPath = normalizePath(rest.substr(0, spacePos));
        newPath = normalizePath(rest.substr(spacePos + 1));
    }
}

bool parseRange(const std::string& tok, int& start, int& count) {
    if (tok.empty()) return false;
    char sign = tok[0];
    if (sign != '-' && sign != '+') return false;
    std::string rest = tok.substr(1);
    if (rest.empty()) return false;
    auto commaPos = rest.find(',');
    if (commaPos == std::string::npos) {
        try {
            start = std::stoi(rest);
            count = 1;
            return true;
        } catch (...) { return false; }
    }
    try {
        start = std::stoi(rest.substr(0, commaPos));
        std::string countStr = rest.substr(commaPos + 1);
        count = countStr.empty() ? 0 : std::stoi(countStr);
        return true;
    } catch (...) { return false; }
}

bool parseHunkHeader(const std::string& line, Hunk& h) {
    if (line.substr(0, 2) != "@@") return false;
    auto end = line.find("@@", 2);
    if (end == std::string::npos) return false;
    std::string body = line.substr(2, end - 2);
    while (!body.empty() && body.front() == ' ') body.erase(body.begin());
    while (!body.empty() && body.back() == ' ') body.pop_back();

    auto spacePos = body.find(' ');
    if (spacePos == std::string::npos) return false;
    std::string oldTok = body.substr(0, spacePos);
    std::string newTok = body.substr(spacePos + 1);
    while (!newTok.empty() && newTok.front() == ' ') newTok.erase(newTok.begin());
    auto spacePos2 = newTok.find(' ');
    if (spacePos2 != std::string::npos) newTok = newTok.substr(0, spacePos2);

    if (!parseRange(oldTok, h.oldStart, h.oldCount)) return false;
    if (!parseRange(newTok, h.newStart, h.newCount)) return false;
    return true;
}

}

std::vector<FilePatch> parseUnifiedDiff(const std::string& input, std::string& error) {
    std::string diffText = extractDiffBlock(input);
    auto lines = splitLines(diffText);

    std::vector<FilePatch> patches;
    FilePatch* cur = nullptr;
    Hunk* curHunk = nullptr;

    std::vector<FilePatch> storage;
    storage.reserve(16);

    auto flushHunk = [&]() {
        if (!cur || !curHunk) return;
        cur->hunks.push_back(*curHunk);
        curHunk = nullptr;
    };

    auto flushFile = [&]() {
        if (!cur) return;
        flushHunk();
        if (!cur->oldPath.empty() || !cur->newPath.empty()) {
            patches.push_back(*cur);
        }
        cur = nullptr;
    };

    Hunk tmpHunk;
    FilePatch tmpPatch;

    for (const auto& raw : lines) {
        std::string line = raw;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.size() >= 11 && line.substr(0, 11) == "diff --git ") {
            flushFile();
            tmpPatch = FilePatch{};
            parseDiffGitLine(line, tmpPatch.oldPath, tmpPatch.newPath);
            storage.push_back(tmpPatch);
            cur = &storage.back();
            continue;
        }

        if (line.size() >= 4 && line.substr(0, 4) == "--- ") {
            if (!cur) {
                tmpPatch = FilePatch{};
                storage.push_back(tmpPatch);
                cur = &storage.back();
            }
            cur->oldPath = normalizePath(line.substr(4));
            continue;
        }
        if (line.size() >= 4 && line.substr(0, 4) == "+++ ") {
            if (!cur) {
                tmpPatch = FilePatch{};
                storage.push_back(tmpPatch);
                cur = &storage.back();
            }
            cur->newPath = normalizePath(line.substr(4));
            continue;
        }

        if (line.size() >= 3 && line.substr(0, 3) == "@@ ") {
            if (!cur) {
                tmpPatch = FilePatch{};
                storage.push_back(tmpPatch);
                cur = &storage.back();
            }
            flushHunk();
            tmpHunk = Hunk{};
            if (!parseHunkHeader(line, tmpHunk)) {
                error = "invalid hunk header: " + line;
                return {};
            }
            curHunk = &tmpHunk;
            continue;
        }

        if (curHunk) {
            if (line == "\\ No newline at end of file") continue;
            if (line.empty()) {
                curHunk->lines.push_back(HunkLine{LineKind::Context, ""});
                continue;
            }
            switch (line[0]) {
            case ' ':
                curHunk->lines.push_back(HunkLine{LineKind::Context, line.substr(1)});
                break;
            case '+':
                if (line.substr(0, 3) == "+++") continue;
                curHunk->lines.push_back(HunkLine{LineKind::Add, line.substr(1)});
                break;
            case '-':
                if (line.substr(0, 3) == "---") continue;
                curHunk->lines.push_back(HunkLine{LineKind::Delete, line.substr(1)});
                break;
            default:
                error = "invalid hunk line: " + line;
                return {};
            }
            continue;
        }
    }

    flushFile();

    if (patches.empty()) {
        error = "no file diffs found";
        return {};
    }
    for (const auto& p : patches) {
        if (p.hunks.empty() && p.oldPath != "/dev/null" && p.newPath != "/dev/null") {
            error = "no hunks found";
            return {};
        }
    }

    return patches;
}

std::string applyPatch(const std::string& oldContent, const FilePatch& patch,
                       std::string& error) {
    bool hasFinalNewline = !oldContent.empty() && oldContent.back() == '\n';
    auto oldLines = splitContentLines(oldContent);

    std::vector<std::string> out;
    out.reserve(oldLines.size());
    int oldIdx = 0;

    for (const auto& h : patch.hunks) {
        int target = h.oldStart - 1;
        if (target < 0) target = 0;
        if (target > static_cast<int>(oldLines.size())) {
            error = "hunk starts beyond end of file: " + std::to_string(h.oldStart);
            return "";
        }
        for (int i = oldIdx; i < target; ++i) {
            out.push_back(oldLines[i]);
        }
        oldIdx = target;

        for (const auto& hl : h.lines) {
            switch (hl.kind) {
            case LineKind::Context:
                if (oldIdx >= static_cast<int>(oldLines.size())) {
                    error = "context exceeds file length";
                    return "";
                }
                if (oldLines[oldIdx] != hl.text) {
                    error = "context mismatch at line " + std::to_string(oldIdx + 1);
                    return "";
                }
                out.push_back(hl.text);
                ++oldIdx;
                break;
            case LineKind::Delete:
                if (oldIdx >= static_cast<int>(oldLines.size())) {
                    error = "delete exceeds file length";
                    return "";
                }
                if (oldLines[oldIdx] != hl.text) {
                    error = "delete mismatch at line " + std::to_string(oldIdx + 1);
                    return "";
                }
                ++oldIdx;
                break;
            case LineKind::Add:
                out.push_back(hl.text);
                break;
            }
        }
    }

    for (int i = oldIdx; i < static_cast<int>(oldLines.size()); ++i) {
        out.push_back(oldLines[i]);
    }

    std::string newContent;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i > 0) newContent += "\n";
        newContent += out[i];
    }
    if (hasFinalNewline || oldContent.empty()) {
        newContent += "\n";
    }

    return newContent;
}

}
}
