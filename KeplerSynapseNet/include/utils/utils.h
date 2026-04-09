#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace synapse {
namespace utils {

class Formatter {
public:
    static std::string formatBytes(uint64_t bytes);
    static std::string formatNumber(uint64_t num);
    static std::string formatCurrency(uint64_t amount, int decimals = 8);
    static std::string formatDuration(uint64_t seconds);
    static std::string formatTimestamp(uint64_t timestamp);
    static std::string formatDate(uint64_t timestamp);
    static std::string formatTime(uint64_t timestamp);
    static std::string formatRelativeTime(uint64_t timestamp);
    static std::string formatPercent(double value, int precision = 1);
    static std::string formatHash(const std::string& hash, int prefixLen = 8, int suffixLen = 8);
    static std::string formatAddress(const std::string& address);
    static std::string formatRate(double rate, const std::string& unit = "/s");
    static std::string formatProgress(double progress, int width = 20);
    static std::string padLeft(const std::string& str, size_t width, char padChar = ' ');
    static std::string padRight(const std::string& str, size_t width, char padChar = ' ');
    static std::string center(const std::string& str, size_t width, char padChar = ' ');
    static std::string truncate(const std::string& str, size_t maxLen, const std::string& suffix = "...");
    static std::string toUpper(const std::string& str);
    static std::string toLower(const std::string& str);
    static std::string trim(const std::string& str);
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string join(const std::vector<std::string>& parts, const std::string& delimiter);
    static std::string repeat(const std::string& str, int count);
    static std::string hexEncode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> hexDecode(const std::string& hex);
    static std::string base64Encode(const std::vector<uint8_t>& data);
    static std::string escapeJson(const std::string& str);
    static std::string formatJson(const std::string& key, const std::string& value);
    static std::string formatJsonNumber(const std::string& key, int64_t value);
    static std::string formatJsonBool(const std::string& key, bool value);
};

class TableFormatter {
public:
    TableFormatter();
    void setHeaders(const std::vector<std::string>& hdrs);
    void addRow(const std::vector<std::string>& row);
    std::string render();
    std::string renderRow(const std::vector<std::string>& row);
    std::string renderSeparator();
    void clear();
    
private:
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<size_t> columnWidths;
    char borderChar;
    char headerSeparator;
};

}
}
