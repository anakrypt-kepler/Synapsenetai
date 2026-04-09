#include "utils/utils.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace synapse {
namespace utils {

std::string Formatter::formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    double size = bytes;
    while (size >= 1024 && unit < 5) { size /= 1024; unit++; }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return ss.str();
}

std::string Formatter::formatNumber(uint64_t num) {
    std::string str = std::to_string(num);
    int insertPosition = str.length() - 3;
    while (insertPosition > 0) { str.insert(insertPosition, ","); insertPosition -= 3; }
    return str;
}

std::string Formatter::formatCurrency(uint64_t amount, int decimals) {
    double value = amount / std::pow(10, decimals);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimals) << value << " NGT";
    return ss.str();
}

std::string Formatter::formatDuration(uint64_t seconds) {
    if (seconds < 60) return std::to_string(seconds) + "s";
    else if (seconds < 3600) return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
    else if (seconds < 86400) return std::to_string(seconds / 3600) + "h " + std::to_string((seconds % 3600) / 60) + "m";
    else return std::to_string(seconds / 86400) + "d " + std::to_string((seconds % 86400) / 3600) + "h";
}

std::string Formatter::formatTimestamp(uint64_t timestamp) {
    time_t ts = timestamp / 1000;
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&ts));
    return std::string(buf);
}

std::string Formatter::formatDate(uint64_t timestamp) {
    time_t ts = timestamp / 1000;
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&ts));
    return std::string(buf);
}

std::string Formatter::formatTime(uint64_t timestamp) {
    time_t ts = timestamp / 1000;
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&ts));
    return std::string(buf);
}

std::string Formatter::formatRelativeTime(uint64_t timestamp) {
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = (now - timestamp) / 1000;
    if (diff < 0) return "in the future";
    else if (diff < 60) return "just now";
    else if (diff < 3600) return std::to_string(diff / 60) + " minutes ago";
    else if (diff < 86400) return std::to_string(diff / 3600) + " hours ago";
    else if (diff < 604800) return std::to_string(diff / 86400) + " days ago";
    else return formatDate(timestamp);
}

std::string Formatter::formatPercent(double value, int precision) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << value << "%";
    return ss.str();
}

std::string Formatter::formatHash(const std::string& hash, int prefixLen, int suffixLen) {
    if (hash.length() <= static_cast<size_t>(prefixLen + suffixLen + 3)) return hash;
    return hash.substr(0, prefixLen) + "..." + hash.substr(hash.length() - suffixLen);
}

std::string Formatter::formatAddress(const std::string& address) { return formatHash(address, 10, 6); }

std::string Formatter::formatRate(double rate, const std::string& unit) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << rate << " " << unit;
    return ss.str();
}

std::string Formatter::formatProgress(double progress, int width) {
    int filled = static_cast<int>(progress / 100.0 * width);
    std::string bar = "[";
    for (int i = 0; i < width; i++) bar += (i < filled) ? "=" : " ";
    bar += "] " + formatPercent(progress, 0);
    return bar;
}

std::string Formatter::padLeft(const std::string& str, size_t width, char padChar) {
    if (str.length() >= width) return str;
    return std::string(width - str.length(), padChar) + str;
}

std::string Formatter::padRight(const std::string& str, size_t width, char padChar) {
    if (str.length() >= width) return str;
    return str + std::string(width - str.length(), padChar);
}

std::string Formatter::center(const std::string& str, size_t width, char padChar) {
    if (str.length() >= width) return str;
    size_t padding = width - str.length();
    return std::string(padding / 2, padChar) + str + std::string(padding - padding / 2, padChar);
}

std::string Formatter::truncate(const std::string& str, size_t maxLen, const std::string& suffix) {
    if (str.length() <= maxLen) return str;
    return str.substr(0, maxLen - suffix.length()) + suffix;
}

std::string Formatter::toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string Formatter::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string Formatter::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Formatter::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) result.push_back(item);
    return result;
}

std::string Formatter::join(const std::vector<std::string>& parts, const std::string& delimiter) {
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += delimiter;
        result += parts[i];
    }
    return result;
}

std::string Formatter::repeat(const std::string& str, int count) {
    std::string result;
    for (int i = 0; i < count; i++) result += str;
    return result;
}

std::string Formatter::hexEncode(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    for (uint8_t b : data) ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return ss.str();
}

std::vector<uint8_t> Formatter::hexDecode(const std::string& hex) {
    std::vector<uint8_t> result;
    for (size_t i = 0; i < hex.length(); i += 2) {
        result.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    }
    return result;
}

std::string Formatter::base64Encode(const std::vector<uint8_t>& data) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < data.size()) n |= data[i + 1] << 8;
        if (i + 2 < data.size()) n |= data[i + 2];
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < data.size()) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < data.size()) ? chars[n & 0x3F] : '=';
    }
    return result;
}

std::string Formatter::escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string Formatter::formatJson(const std::string& key, const std::string& value) {
    return "\"" + escapeJson(key) + "\": \"" + escapeJson(value) + "\"";
}

std::string Formatter::formatJsonNumber(const std::string& key, int64_t value) {
    return "\"" + escapeJson(key) + "\": " + std::to_string(value);
}

std::string Formatter::formatJsonBool(const std::string& key, bool value) {
    return "\"" + escapeJson(key) + "\": " + (value ? "true" : "false");
}

TableFormatter::TableFormatter() : borderChar('|'), headerSeparator('-') {}

void TableFormatter::setHeaders(const std::vector<std::string>& hdrs) {
    headers = hdrs;
    columnWidths.resize(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); i++) {
        columnWidths[i] = std::max(columnWidths[i], headers[i].length());
    }
}

void TableFormatter::addRow(const std::vector<std::string>& row) {
    rows.push_back(row);
    for (size_t i = 0; i < row.size() && i < columnWidths.size(); i++) {
        columnWidths[i] = std::max(columnWidths[i], row[i].length());
    }
}

std::string TableFormatter::render() {
    std::stringstream ss;
    ss << renderRow(headers);
    ss << renderSeparator();
    for (const auto& row : rows) ss << renderRow(row);
    return ss.str();
}

std::string TableFormatter::renderRow(const std::vector<std::string>& row) {
    std::stringstream ss;
    ss << borderChar;
    for (size_t i = 0; i < columnWidths.size(); i++) {
        std::string cell = (i < row.size()) ? row[i] : "";
        ss << " " << Formatter::padRight(cell, columnWidths[i]) << " " << borderChar;
    }
    ss << "\n";
    return ss.str();
}

std::string TableFormatter::renderSeparator() {
    std::stringstream ss;
    ss << borderChar;
    for (size_t width : columnWidths) ss << std::string(width + 2, headerSeparator) << borderChar;
    ss << "\n";
    return ss.str();
}

void TableFormatter::clear() {
    headers.clear();
    rows.clear();
    columnWidths.clear();
}

}
}
