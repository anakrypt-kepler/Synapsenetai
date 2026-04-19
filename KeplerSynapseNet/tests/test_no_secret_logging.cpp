#include <cassert>
#include <string>
#include <iostream>
#include <vector>

static std::string sanitizeLogMessage(const std::string& in) {
    std::string s = in;
    const std::vector<std::string> keys = {"password", "pass", "pwd", "secret", "private_key", "privkey", "mnemonic", "seed"};
    for (const auto& k : keys) {
        size_t pos = 0;
        while ((pos = s.find(k, pos)) != std::string::npos) {
            size_t i = pos + k.size();
            while (i < s.size() && (s[i] == ' ' || s[i] == '"' || s[i] == '\'' || s[i] == ':' || s[i] == '=')) i++;
            size_t sep = i;
            size_t end = sep;
            while (end < s.size() && s[end] != '"' && s[end] != '\'' && s[end] != ' ' && s[end] != ',' && s[end] != ')' && s[end] != ';' && s[end] != '\n') end++;
            if (sep < end) {
                s.replace(sep, end - sep, "[REDACTED]");
                pos = sep + 9;
            } else pos += k.size();
        }
    }
    return s;
}

int main() {
    std::string msg1 = "user=alice password=hunter2";
    std::string out1 = sanitizeLogMessage(msg1);
    assert(out1.find("[REDACTED]") != std::string::npos);

    std::string msg2 = "mnemonic: abandon abandon abandon, seed=abcdef";
    std::string out2 = sanitizeLogMessage(msg2);
    assert(out2.find("[REDACTED]") != std::string::npos);

    std::string msg3 = "private_key='0123456789'";
    std::string out3 = sanitizeLogMessage(msg3);
    assert(out3.find("[REDACTED]") != std::string::npos);

    std::string msg4 = "seed=deadbeef; other=ok";
    std::string out4 = sanitizeLogMessage(msg4);
    assert(out4.find("[REDACTED]") != std::string::npos);

    std::string normal = "this is a normal log message with no credentials";
    std::string outn = sanitizeLogMessage(normal);
    assert(outn == normal);

    std::cout << "test_no_secret_logging PASSED\n";
    return 0;
}
