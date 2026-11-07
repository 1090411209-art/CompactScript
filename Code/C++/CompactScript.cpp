// CompactScript - C++17 terminal implementation.
// Build: g++ -std=c++17 -O2 -pthread CompactScript.cpp -o CompactScript

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <climits>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

// Serializes all stdout writes; background threads (wait/time/repeat) share this.
static std::mutex g_outMutex;

static void rawOut(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_outMutex);
    std::cout << s << std::flush;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string ltrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a);
}

static bool startsWithCI(const std::string& s, const std::string& p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i])) return false;
    return true;
}

static std::string toLower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = std::tolower((unsigned char)c);
    return r;
}

// ANSI 24-bit red error wrapper used everywhere.
static std::string err(const std::string& m) {
    return "\033[38;2;255;85;85merror: " + m + "\033[0m";
}

// ANSI 24-bit grey hint wrapper.
static std::string hint(const std::string& m) {
    return "\033[38;2;136;136;136m" + m + "\033[0m";
}

// Fixed 10-decimal formatting with trailing-zero stripping; -0 collapses to 0.
static std::string formatNumber(double num) {
    if (std::isnan(num) || std::isinf(num)) return "";
    std::ostringstream o;
    o << std::fixed << std::setprecision(10) << num;
    std::string s = o.str();
    size_t d = s.find('.');
    if (d != std::string::npos) {
        size_t l = s.find_last_not_of('0');
        if (l == d) l--;
        s = s.substr(0, l + 1);
    }
    if (s == "-0") s = "0";
    return s;
}

static bool isValidNumber(const std::string& str) {
    static const std::regex re(R"RX(^[-+]?(\d+\.?\d*|\.\d+)$)RX");
    std::string t = trim(str);
    return !t.empty() && std::regex_match(t, re);
}

// Empty return means the name is valid; otherwise it returns a human-readable reason.
static std::string varNameError(const std::string& name) {
    if (name.empty()) return "variable name cannot be empty";
    if (!(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
        return "invalid variable name \"" + name + "\": must start with a letter or underscore";
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            return "invalid variable name \"" + name + "\": only letters, digits and underscores are allowed";
    return "";
}

static const std::map<std::string, std::string>& getColorMap() {
    static const std::map<std::string, std::string> m = {
        {"red","#ff0000"},{"light_red","#ff6666"},{"dark_red","#990000"},
        {"green","#00ff00"},{"light_green","#66ff66"},{"dark_green","#009900"},
        {"blue","#0000ff"},{"light_blue","#6666ff"},{"dark_blue","#000099"},
        {"yellow","#ffff00"},{"light_yellow","#ffff66"},{"dark_yellow","#999900"},
        {"cyan","#00ffff"},{"light_cyan","#66ffff"},{"dark_cyan","#009999"},
        {"magenta","#ff00ff"},{"light_magenta","#ff66ff"},{"dark_magenta","#990099"},
        {"orange","#ffa500"},{"light_orange","#ffcc66"},{"dark_orange","#cc8400"},
        {"purple","#800080"},{"light_purple","#cc66cc"},{"dark_purple","#4d004d"}
    };
    return m;
}

static bool hasColor(const std::string& n) {
    return getColorMap().count(n) > 0;
}

static std::string colorError(const std::string& n) {
    if (!hasColor(n)) {
        std::string a;
        for (auto& p : getColorMap()) { if (!a.empty()) a += ", "; a += p.first; }
        return "unknown color \"" + n + "\". Available colors: " + a;
    }
    return "";
}

// "#RRGGBB" -> "\033[38;2;R;G;Bm" 24-bit foreground escape.
static std::string colorAnsi(const std::string& n) {
    auto it = getColorMap().find(n);
    if (it == getColorMap().end()) return "";
    const std::string& h = it->second;
    int r = std::stoi(h.substr(1, 2), nullptr, 16);
    int g = std::stoi(h.substr(3, 2), nullptr, 16);
    int b = std::stoi(h.substr(5, 2), nullptr, 16);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", r, g, b);
    return buf;
}

struct Segment {
    std::string color;
    std::string text;
};

static std::string renderSegments(const std::vector<Segment>& segs) {
    std::string o;
    for (auto& s : segs) {
        if (s.text == "\n") { o += '\n'; continue; }
        if (!s.color.empty() && hasColor(s.color)) o += colorAnsi(s.color) + s.text + "\033[0m";
        else o += s.text;
    }
    return o;
}

// Either a plain text/number or a multi-colour sequence of segments.
struct Variable {
    bool isText = true;
    std::string str;
    double num{};
    std::string color;
    std::vector<Segment> segments;
    std::string toString() const { return isText ? str : formatNumber(num); }
};

static std::string renderVariable(const Variable& v) {
    if (!v.segments.empty()) return renderSegments(v.segments);
    std::string t = v.toString();
    if (!v.color.empty() && hasColor(v.color)) return colorAnsi(v.color) + t + "\033[0m";
    return t;
}

static std::map<std::string, Variable> variables;
static std::map<std::string, std::vector<std::string>> functions;
static std::map<std::string, std::map<std::string, std::string>> lists;

struct VarRead {
    bool ok{};
    bool isText{};
    std::string text;
    double num{};
    std::string error;
};

// Reads a variable as either text or number with a uniform {ok,isText,text,num} shape.
static VarRead readVarValue(const std::string& name, const std::string& pfx) {
    VarRead r{};
    std::string ne = varNameError(name);
    if (!ne.empty()) { r.ok = false; r.error = pfx + ": " + ne; return r; }
    auto it = variables.find(name);
    if (it == variables.end()) { r.ok = false; r.error = pfx + ": variable \"" + name + "\" not found"; return r; }
    r.ok = true;
    if (it->second.isText) { r.isText = true; r.text = it->second.str; }
    else { r.isText = false; r.num = it->second.num; r.text = formatNumber(it->second.num); }
    return r;
}

// Non-ASCII / non-printable bytes become '?'. Newlines/tabs are preserved.
static std::string sanitizeAscii(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c >= 32 && c <= 126) r += (char)c;
        else if (c == '\n' || c == '\r' || c == '\t') r += (char)c;
        else r += '?';
    }
    return r;
}

// Strips `#"..."` only when the '#' is the first non-whitespace char of a line.
// An inline '#' (preceded by anything other than whitespace) is treated as literal text.
static std::string stripComments(const std::string& s) {
    std::string r;
    size_t i = 0;
    bool inStr = false;
    while (i < s.size()) {
        char c = s[i];
        if (!inStr && c == '#' && i + 1 < s.size() && s[i + 1] == '"') {
            long k = (long)i - 1;
            while (k >= 0 && (s[k] == ' ' || s[k] == '\t')) k--;
            if (k < 0 || s[k] == '\n' || s[k] == '\r') {
                size_t j = i + 2;
                while (j < s.size() && s[j] != '"') j++;
                if (j < s.size()) { i = j + 1; continue; }
            }
        }
        if (c == '"') inStr = !inStr;
        r += c;
        i++;
    }
    return r;
}

// Removes newlines that appear OUTSIDE string literals.
// This is what lets a multi-line block from the REPL become one logical command.
// Inside "..." newlines are preserved.
static std::string stripNewlinesOutsideStrings(const std::string& s) {
    std::string r;
    bool inStr = false;
    for (char c : s) {
        if (c == '"') { inStr = !inStr; r += c; continue; }
        if ((c == '\n' || c == '\r') && !inStr) continue;
        r += c;
    }
    return r;
}

struct ParenResult {
    bool found{};
    std::string content;
    size_t endPos{};
};

// Extracts a balanced (...) block. Handles nested parens and quotes.
static ParenResult extractParenthesized(const std::string& s, size_t startPos) {
    size_t i = startPos;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= s.size() || s[i] != '(') return { false, "", 0 };
    int depth = 0;
    bool inStr = false;
    size_t start = i;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == '"') inStr = false; continue; }
        if (c == '"') { inStr = true; continue; }
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) return { true, trim(s.substr(start + 1, i - start - 1)), i + 1 };
        }
    }
    return { false, "", 0 };
}

struct ValueToken {
    std::string type;
    std::string value;
};

// Tokenizes a value expression such as: "abc"-n-"def"-color["red"]-"ghi"
// Tokens:
//   "string"        -> literal text
//   -n-             -> newline
//   -nr-            -> newline + reset colour
//   -r-             -> reset colour
//   -color["name"]  -> switch colour
//   "dash"          -> stray '-' (kept for compatibility / error reporting)
static bool tokenizeValueExpr(const std::string& str, std::vector<ValueToken>& tokens, std::string& error) {
    size_t i = 0;
    while (i < str.size()) {
        while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
        if (i >= str.size()) break;
        if (str[i] == '"') {
            size_t j = i + 1;
            while (j < str.size() && str[j] != '"') j++;
            if (j >= str.size()) { error = "unterminated string literal"; return false; }
            tokens.push_back({ "string", str.substr(i + 1, j - i - 1) });
            i = j + 1;
            continue;
        }
        if (str[i] == '-') {
            i++;
            while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
            if (i < str.size() && std::tolower((unsigned char)str[i]) == 'n') {
                size_t saveN = i;
                i++;
                while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                if (i < str.size() && std::tolower((unsigned char)str[i]) == 'r') {
                    size_t saveR = i;
                    i++;
                    while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                    if (i < str.size() && str[i] == '-') {
                        i++;
                        tokens.push_back({ "newline-reset", "" });
                        while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                        std::smatch m;
                        std::string sub = str.substr(i);
                        static const std::regex colorRe(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
                        if (std::regex_search(sub, m, colorRe)) { tokens.push_back({ "color", m[1] }); i += m.length(); }
                        continue;
                    }
                    i = saveR;
                }
                if (i < str.size() && str[i] == '-') {
                    i++;
                    tokens.push_back({ "newline", "" });
                    while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                    std::smatch m;
                    std::string sub = str.substr(i);
                    static const std::regex colorRe2(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
                    if (std::regex_search(sub, m, colorRe2)) { tokens.push_back({ "color", m[1] }); i += m.length(); }
                    continue;
                }
                i = saveN;
            }
            if (i < str.size() && std::tolower((unsigned char)str[i]) == 'r') {
                size_t saveR = i;
                i++;
                while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                if (i < str.size() && str[i] == '-') {
                    i++;
                    tokens.push_back({ "reset", "" });
                    while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                    std::smatch m;
                    std::string sub = str.substr(i);
                    static const std::regex colorRe3(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
                    if (std::regex_search(sub, m, colorRe3)) { tokens.push_back({ "color", m[1] }); i += m.length(); }
                    continue;
                }
                i = saveR;
            }
            std::smatch m;
            std::string sub = str.substr(i);
            static const std::regex colorRe4(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
            if (std::regex_search(sub, m, colorRe4)) { tokens.push_back({ "color", m[1] }); i += m.length(); continue; }
            tokens.push_back({ "dash", "" });
            continue;
        }
        error = "unexpected character '" + std::string(1, str[i]) + "'";
        return false;
    }
    return true;
}

// Turns a token list into Segment{color,text} list. A colour specified at the
// very end with no following text retroactively applies to the previous segment.
static bool parseMultiColorValue(const std::string& str, const std::string& init, bool allowNewlines, std::vector<Segment>& out, std::string& error) {
    std::vector<ValueToken> tokens;
    if (!tokenizeValueExpr(str, tokens, error)) return false;
    if (tokens.empty()) { error = "empty value expression"; return false; }
    out.clear();
    std::string curColor = init, trailing;
    for (auto& t : tokens) {
        if (t.type == "string") {
            if (!allowNewlines && t.value.find('\n') != std::string::npos) { error = "newlines are not allowed in this expression"; return false; }
            out.push_back({ curColor, t.value });
            trailing.clear();
        } else if (t.type == "newline") {
            if (!allowNewlines) { error = "newlines are not allowed in this expression"; return false; }
            out.push_back({ "", "\n" });
            trailing.clear();
        } else if (t.type == "newline-reset") {
            if (!allowNewlines) { error = "newlines are not allowed in this expression"; return false; }
            out.push_back({ "", "\n" });
            curColor.clear();
            trailing.clear();
        } else if (t.type == "reset") {
            curColor.clear();
            trailing.clear();
        } else if (t.type == "color") {
            std::string ce = colorError(t.value);
            if (!ce.empty()) { error = ce; return false; }
            curColor = t.value;
            trailing = t.value;
        }
    }
    if (out.empty()) { error = "empty value expression"; return false; }
    if (!trailing.empty())
        for (int k = (int)out.size() - 1; k >= 0; --k)
            if (out[k].text != "\n") { out[k].color = trailing; break; }
    return true;
}

// Builds a Variable from segments. For number: joins texts and requires a valid number.
// If exactly one colour is used, also stores it in `color` for single-colour fast path.
static bool buildVariable(bool wantNum, const std::vector<Segment>& segs, bool allowNewlines, Variable& out, std::string& error) {
    if (segs.empty()) { error = "empty value"; return false; }
    bool hasColor = false, hasNl = false;
    std::set<std::string> colors;
    std::string full;
    for (auto& s : segs) {
        if (s.text == "\n") hasNl = true;
        if (!s.color.empty()) { hasColor = true; colors.insert(s.color); }
        full += s.text;
    }
    if (!allowNewlines && hasNl) { error = "newlines are not allowed in this expression"; return false; }
    if (wantNum) {
        if (!isValidNumber(full)) { error = "\"" + full + "\" is not a valid number"; return false; }
        out = Variable{};
        out.isText = false;
        out.num = std::stod(full);
        if (hasColor || hasNl) {
            out.segments = segs;
            if (colors.size() == 1 && !hasNl) out.color = *colors.begin();
        }
        return true;
    }
    out = Variable{};
    out.isText = true;
    out.str = full;
    if (hasColor || hasNl) {
        out.segments = segs;
        if (colors.size() == 1) out.color = *colors.begin();
    }
    return true;
}

struct Operand {
    std::string kind;   // "num" | "text" | "var"
    std::string value;  // literal value for num/text
    std::string name;   // variable name for var
    size_t length{};    // bytes consumed
};

// Parses one operand from the start of the string.
// Accepted forms: -n"123"  -t"abc"  ["varName"]  n"123"  t"abc"
static bool parseOperandAt(const std::string& str, Operand& out, size_t& consumed) {
    std::smatch m;
    static const std::regex numRe(R"RX(^-\s*n\s*"([^"]*)")RX", std::regex::icase);
    static const std::regex txtRe(R"RX(^-\s*t\s*"([^"]*)")RX", std::regex::icase);
    static const std::regex varRe(R"RX(^\[\s*"([^"]*)"\s*\])RX");
    static const std::regex numRe2(R"RX(^n\s*"([^"]*)")RX", std::regex::icase);
    static const std::regex txtRe2(R"RX(^t\s*"([^"]*)")RX", std::regex::icase);
    if (std::regex_search(str, m, numRe)) { out = { "num", m[1], "", (size_t)m.length() }; consumed = m.length(); return true; }
    if (std::regex_search(str, m, txtRe)) { out = { "text", m[1], "", (size_t)m.length() }; consumed = m.length(); return true; }
    if (std::regex_search(str, m, varRe)) { out = { "var", "", m[1], (size_t)m.length() }; consumed = m.length(); return true; }
    if (std::regex_search(str, m, numRe2)) { out = { "num", m[1], "", (size_t)m.length() }; consumed = m.length(); return true; }
    if (std::regex_search(str, m, txtRe2)) { out = { "text", m[1], "", (size_t)m.length() }; consumed = m.length(); return true; }
    return false;
}

struct ResolvedOperand {
    bool ok{};
    bool isNum{};
    double num{};
    std::string text;
    std::string error;
};

static ResolvedOperand resolveOperand(const Operand& o) {
    ResolvedOperand r{};
    if (o.kind == "var") {
        std::string ne = varNameError(o.name);
        if (!ne.empty()) { r.ok = false; r.error = ne; return r; }
        auto it = variables.find(o.name);
        if (it == variables.end()) { r.ok = false; r.error = "variable \"" + o.name + "\" not found"; return r; }
        r.ok = true;
        if (it->second.isText) { r.isNum = false; r.text = it->second.str; }
        else { r.isNum = true; r.num = it->second.num; }
        return r;
    }
    if (o.kind == "num") {
        if (!isValidNumber(o.value)) { r.ok = false; r.error = "literal \"" + o.value + "\" is not a valid number"; return r; }
        r.ok = true;
        r.isNum = true;
        r.num = std::stod(o.value);
        return r;
    }
    r.ok = true;
    r.isNum = false;
    r.text = o.value;
    return r;
}

struct ConditionResult {
    bool ok{};
    std::string error;
    Operand a, b;
    std::string op;
    std::string remaining;
    // Numbers are compared numerically. Texts are compared by LENGTH (not lexicographically).
    bool check() const {
        auto ra = resolveOperand(a), rb = resolveOperand(b);
        if (!ra.ok || !rb.ok) return false;
        if (ra.isNum != rb.isNum) return false;
        if (ra.isNum) {
            if (op == "eq") return ra.num == rb.num;
            if (op == "ne") return ra.num != rb.num;
            if (op == "gt") return ra.num > rb.num;
            if (op == "lt") return ra.num < rb.num;
            if (op == "ge") return ra.num >= rb.num;
            if (op == "le") return ra.num <= rb.num;
        } else {
            size_t l1 = ra.text.size(), l2 = rb.text.size();
            if (op == "eq") return l1 == l2;
            if (op == "ne") return l1 != l2;
            if (op == "gt") return l1 > l2;
            if (op == "lt") return l1 < l2;
            if (op == "ge") return l1 >= l2;
            if (op == "le") return l1 <= l2;
        }
        return false;
    }
};

// Parses `<operand> <op> <operand>` and leaves the remainder for the caller.
static ConditionResult buildCondition(const std::string& str, const std::string& pfx) {
    ConditionResult res{};
    Operand op1;
    size_t used = 0;
    if (!parseOperandAt(str, op1, used)) { res.error = pfx + ": expected first operand"; return res; }
    std::string rest = trim(str.substr(used));
    std::smatch m;
    static const std::regex opRe(R"RX(^-?\s*(eq|ne|gt|lt|ge|le)\s*-?\s*)RX", std::regex::icase);
    if (!std::regex_search(rest, m, opRe)) { res.error = pfx + ": expected comparison operator"; return res; }
    std::string op = toLower(m[1]);
    rest = trim(rest.substr(m.length()));
    Operand op2;
    if (!parseOperandAt(rest, op2, used)) { res.error = pfx + ": expected second operand"; return res; }
    rest = trim(rest.substr(used));
    // Pre-validate both operands so errors surface before evaluating.
    auto pre = [&](const Operand& o) -> std::string {
        if (o.kind == "var") {
            std::string ne = varNameError(o.name);
            if (!ne.empty()) return pfx + ": " + ne;
            if (!variables.count(o.name)) return pfx + ": variable \"" + o.name + "\" not found";
        } else if (o.kind == "num") {
            if (!isValidNumber(o.value)) return pfx + ": literal \"" + o.value + "\" is not a valid number";
        }
        return "";
    };
    std::string e1 = pre(op1);
    if (!e1.empty()) { res.error = e1; return res; }
    std::string e2 = pre(op2);
    if (!e2.empty()) { res.error = e2; return res; }
    auto r1 = resolveOperand(op1), r2 = resolveOperand(op2);
    if (!r1.ok) { res.error = pfx + ": " + r1.error; return res; }
    if (!r2.ok) { res.error = pfx + ": " + r2.error; return res; }
    if (r1.isNum != r2.isNum) { res.error = pfx + ": cannot compare number with text"; return res; }
    res.ok = true;
    res.a = op1;
    res.b = op2;
    res.op = op;
    res.remaining = rest;
    return res;
}

struct BranchResult {
    bool ok{};
    bool isNull{};
    std::string cmd;
    size_t endPos{};
    std::string error;
};

// Parses a branch body: "(cmd)" or "(null)". (null) means "skip this branch".
static BranchResult parseBranch(const std::string& rem, const std::string& pfx, const std::string& label) {
    BranchResult r{};
    auto p = extractParenthesized(rem, 0);
    if (!p.found) { r.ok = false; r.error = pfx + ": " + label + " must be followed by (command) or (null)"; return r; }
    r.ok = true;
    r.endPos = p.endPos;
    if (toLower(trim(p.content)) == "null") { r.isNull = true; r.cmd = ""; }
    else { r.isNull = false; r.cmd = p.content; }
    return r;
}

struct LongPressExecResult {
    bool ok{};
    std::string error;
    std::vector<Segment> promptSegs;
    struct Entry {
        std::string key;
        int time{};
        std::string cmd;
    };
    std::vector<Entry> entries;
};

// Parses: "prompt"-time["N"]"k"=(cmd)-time["N"]"k2"=(cmd2)...
// Prompt text (with colour tokens) comes before the first -time[...].
static LongPressExecResult parseLongPressExec(const std::string& str) {
    LongPressExecResult res{};
    std::vector<ValueToken> promptTokens;
    size_t i = 0;
    bool scanning = true;
    while (i < str.size()) {
        while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
        if (i >= str.size()) break;
        if (str[i] == '-') {
            i++;
            while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
            std::string sub = str.substr(i);
            std::smatch tm;
            static const std::regex timeRe(R"RX(^time\s*\[\s*"(\d+)"\s*\]\s*)RX", std::regex::icase);
            if (std::regex_search(sub, tm, timeRe)) {
                int tsec = std::stoi(tm[1]);
                i += tm.length();
                if (i >= str.size() || str[i] != '"') { res.error = "expected key"; return res; }
                size_t j = i + 1;
                while (j < str.size() && str[j] != '"') j++;
                if (j >= str.size()) { res.error = "unterminated key"; return res; }
                std::string key = str.substr(i + 1, j - i - 1);
                if (key.size() != 1) { res.error = "key must be a single character"; return res; }
                i = j + 1;
                while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                if (i >= str.size() || str[i] != '=') { res.error = "expected '='"; return res; }
                i++;
                while (i < str.size() && std::isspace((unsigned char)str[i])) i++;
                if (i >= str.size() || str[i] != '(') { res.error = "expected '('"; return res; }
                auto p = extractParenthesized(str, i);
                if (!p.found) { res.error = "missing ')'"; return res; }
                res.entries.push_back({ key, tsec, p.content });
                i = p.endPos;
                scanning = false;
                continue;
            }
            static const std::regex colorRe(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
            if (std::regex_search(sub, tm, colorRe)) {
                std::string ce = colorError(tm[1]);
                if (!ce.empty()) { res.error = ce; return res; }
                if (scanning) promptTokens.push_back({ "color", tm[1] });
                i += tm.length();
                continue;
            }
            if (startsWithCI(sub, "nr-")) { if (scanning) promptTokens.push_back({ "newline-reset", "" }); i += 3; continue; }
            if (startsWithCI(sub, "n-")) { if (scanning) promptTokens.push_back({ "newline", "" }); i += 2; continue; }
            if (startsWithCI(sub, "r-")) { if (scanning) promptTokens.push_back({ "reset", "" }); i += 2; continue; }
            continue;
        }
        if (str[i] == '"') {
            size_t j = i + 1;
            while (j < str.size() && str[j] != '"') j++;
            if (j >= str.size()) { res.error = "unterminated string"; return res; }
            if (scanning) promptTokens.push_back({ "string", str.substr(i + 1, j - i - 1) });
            i = j + 1;
            continue;
        }
        res.error = std::string("unexpected character \"") + str[i] + "\"";
        return res;
    }
    if (res.entries.empty()) { res.error = "no time entries specified"; return res; }
    std::string curColor, trailing;
    for (auto& t : promptTokens) {
        if (t.type == "string") { res.promptSegs.push_back({ curColor, t.value }); trailing.clear(); }
        else if (t.type == "color") { curColor = t.value; trailing = t.value; }
        else if (t.type == "newline") { res.promptSegs.push_back({ "", "\n" }); trailing.clear(); }
        else if (t.type == "newline-reset") { res.promptSegs.push_back({ "", "\n" }); curColor.clear(); trailing.clear(); }
        else if (t.type == "reset") { curColor.clear(); trailing.clear(); }
    }
    if (!trailing.empty())
        for (int k = (int)res.promptSegs.size() - 1; k >= 0; --k)
            if (res.promptSegs[k].text != "\n") { res.promptSegs[k].color = trailing; break; }
    res.ok = true;
    return res;
}

// File format: magic prefix, then JSON.
static const std::string CS_MAGIC_TEXT = "CSCRIPT-CSCRIPT-V1\n";
static const std::string CS_MAGIC_XOR = "CSCRIPT-CSCRIPT-XOR-V1\n";
static const std::string FILE_EXT = ".cscript";
static const unsigned char XOR_KEY[16] = {0x5A,0x37,0xC1,0x9E,0x42,0x88,0x1D,0xF6,0x33,0x7B,0xA4,0xE0,0x5C,0x91,0x2F,0xD8};

static std::vector<unsigned char> xorBytes(const std::vector<unsigned char>& in) {
    std::vector<unsigned char> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] ^ XOR_KEY[i % 16];
    return out;
}

static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                r += b;
            } else r += c;
        }
    }
    return r;
}

static std::string varToJson(const Variable& v) {
    std::ostringstream o;
    o << "{\"type\":\"" << (v.isText ? "text" : "number") << "\"";
    if (v.isText) o << ",\"value\":\"" << jsonEscape(v.str) << "\"";
    else o << ",\"value\":" << (std::isnan(v.num) || std::isinf(v.num) ? "0" : formatNumber(v.num));
    if (!v.color.empty()) o << ",\"color\":\"" << jsonEscape(v.color) << "\"";
    if (!v.segments.empty()) {
        o << ",\"segments\":[";
        for (size_t i = 0; i < v.segments.size(); ++i) {
            if (i) o << ",";
            o << "{\"color\":\"" << jsonEscape(v.segments[i].color) << "\",\"text\":\"" << jsonEscape(v.segments[i].text) << "\"}";
        }
        o << "]";
    }
    o << "}";
    return o.str();
}

static std::string saveToFileData() {
    std::ostringstream o;
    o << "{\"variables\":{";
    bool first = true;
    for (auto& p : variables) {
        if (!first) o << ",";
        first = false;
        o << "\"" << jsonEscape(p.first) << "\":" << varToJson(p.second);
    }
    o << "},\"functions\":{";
    first = true;
    for (auto& p : functions) {
        if (!first) o << ",";
        first = false;
        o << "\"" << jsonEscape(p.first) << "\":[";
        for (size_t i = 0; i < p.second.size(); ++i) {
            if (i) o << ",";
            o << "\"" << jsonEscape(p.second[i]) << "\"";
        }
        o << "]";
    }
    o << "},\"lists\":{";
    first = true;
    for (auto& p : lists) {
        if (!first) o << ",";
        first = false;
        o << "\"" << jsonEscape(p.first) << "\":{";
        bool f2 = true;
        for (auto& q : p.second) {
            if (!f2) o << ",";
            f2 = false;
            o << "\"" << jsonEscape(q.first) << "\":\"" << jsonEscape(q.second) << "\"";
        }
        o << "}";
    }
    o << "}}";
    return o.str();
}

// Minimal JSON parser sufficient for .cscript files (no external deps).
namespace JsonP {

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
    enum T { NULLV, BOOLV, NUMV, STRV, ARRV, OBJV } t = NULLV;
    bool b{};
    double n{};
    std::string s;
    std::vector<ValuePtr> arr;
    std::map<std::string, ValuePtr> obj;
};

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    bool parse(ValuePtr& out) {
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') return parseObj(out);
        if (c == '[') return parseArr(out);
        if (c == '"') {
            out = std::make_shared<Value>();
            out->t = Value::STRV;
            return parseStr(out->s);
        }
        if (c == 't' || c == 'f') {
            out = std::make_shared<Value>();
            out->t = Value::BOOLV;
            if (s.compare(i, 4, "true") == 0) { out->b = true; i += 4; return true; }
            if (s.compare(i, 5, "false") == 0) { out->b = false; i += 5; return true; }
            return false;
        }
        if (c == 'n') {
            if (s.compare(i, 4, "null") == 0) {
                out = std::make_shared<Value>();
                out->t = Value::NULLV;
                i += 4;
                return true;
            }
            return false;
        }
        out = std::make_shared<Value>();
        out->t = Value::NUMV;
        size_t st = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
        if (i == st) return false;
        out->n = std::stod(s.substr(st, i - st));
        return true;
    }
    bool parseStr(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[i + 1];
                i += 2;
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    int cp = std::stoi(s.substr(i, 4), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e;
                }
            } else { out += s[i]; i++; }
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    }
    bool parseArr(ValuePtr& out) {
        out = std::make_shared<Value>();
        out->t = Value::ARRV;
        if (s[i] != '[') return false;
        i++;
        ws();
        if (i < s.size() && s[i] == ']') { i++; return true; }
        while (true) {
            ValuePtr v;
            if (!parse(v)) return false;
            out->arr.push_back(v);
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
    bool parseObj(ValuePtr& out) {
        out = std::make_shared<Value>();
        out->t = Value::OBJV;
        if (s[i] != '{') return false;
        i++;
        ws();
        if (i < s.size() && s[i] == '}') { i++; return true; }
        while (true) {
            ws();
            std::string key;
            if (!parseStr(key)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return false;
            i++;
            ValuePtr v;
            if (!parse(v)) return false;
            out->obj[key] = v;
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
};

}

// Replaces all global state from a parsed JSON document.
static bool applyLoadedData(const std::string& jsonText, std::string& errMsg) {
    JsonP::Parser p(jsonText);
    JsonP::ValuePtr root;
    if (!p.parse(root)) { errMsg = "cannot parse JSON"; return false; }
    if (!root || root->t != JsonP::Value::OBJV) { errMsg = "invalid structure"; return false; }
    auto vIt = root->obj.find("variables");
    auto fIt = root->obj.find("functions");
    auto lIt = root->obj.find("lists");
    if (vIt == root->obj.end()) { errMsg = "missing variables"; return false; }
    variables.clear();
    functions.clear();
    lists.clear();
    if (vIt->second->t == JsonP::Value::OBJV)
        for (auto& kv : vIt->second->obj) {
            Variable v;
            auto& obj = kv.second->obj;
            if (obj.count("type") && obj["type"]->s == "number") {
                v.isText = false;
                if (obj.count("value") && obj["value"]->t == JsonP::Value::NUMV) v.num = obj["value"]->n;
            } else {
                v.isText = true;
                if (obj.count("value") && obj["value"]->t == JsonP::Value::STRV) v.str = obj["value"]->s;
            }
            if (obj.count("color")) v.color = obj["color"]->s;
            if (obj.count("segments") && obj["segments"]->t == JsonP::Value::ARRV)
                for (auto& sv : obj["segments"]->arr) {
                    Segment s;
                    if (sv->obj.count("color")) s.color = sv->obj["color"]->s;
                    if (sv->obj.count("text")) s.text = sv->obj["text"]->s;
                    v.segments.push_back(s);
                }
            variables[kv.first] = v;
        }
    if (fIt != root->obj.end() && fIt->second->t == JsonP::Value::OBJV)
        for (auto& kv : fIt->second->obj) {
            std::vector<std::string> body;
            if (kv.second->t == JsonP::Value::ARRV)
                for (auto& x : kv.second->arr) body.push_back(x->s);
            functions[kv.first] = body;
        }
    if (lIt != root->obj.end() && lIt->second->t == JsonP::Value::OBJV)
        for (auto& kv : lIt->second->obj) {
            std::map<std::string, std::string> items;
            if (kv.second->t == JsonP::Value::OBJV)
                for (auto& x : kv.second->obj) items[x.first] = x.second->s;
            lists[kv.first] = items;
        }
    return true;
}

// number[concatenation]: only "+" is allowed; all operands are strings.
static bool evalConcat(const std::string& expr, std::string& result, std::string& error) {
    std::string t = trim(expr);
    if (t.empty()) { error = "empty expression"; return false; }
    static const std::regex anyVarRe(R"RX(\[\s*"([^"]*)"\s*\])RX");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), anyVarRe); it != std::sregex_iterator(); ++it) {
        std::string n = (*it)[1];
        std::string ne = varNameError(n);
        if (!ne.empty()) { error = "invalid variable reference " + it->str() + ": " + ne; return false; }
        if (!variables.count(n)) { error = "variable \"" + n + "\" not found"; return false; }
    }
    std::string stripped = std::regex_replace(t, anyVarRe, "");
    if (stripped.find_first_of("-*/^!") != std::string::npos) { error = "number[concatenation] only supports \"+\""; return false; }
    std::vector<std::string> parts;
    std::string cur;
    for (char c : t) {
        if (c == '+') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    result.clear();
    static const std::regex varRe(R"RX(\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\])RX");
    for (auto& part : parts) {
        std::string p = trim(part);
        if (p.empty()) continue;
        size_t pos = 0;
        while (pos < p.size()) {
            std::smatch m;
            std::string sub = p.substr(pos);
            if (std::regex_search(sub, m, varRe)) {
                size_t st = m.position(0);
                result += sub.substr(0, st);
                result += variables[m[1]].toString();
                pos += st + m.length(0);
            } else {
                result += sub;
                break;
            }
        }
    }
    return true;
}

static bool evalExpression(const std::string& expr, std::string& result, std::string& error);

// computation[modulo]: "a"/"b" -> "quotient......remainder".
static bool evalModulo(const std::string& expr, std::string& result, std::string& error) {
    size_t p = expr.find('/');
    if (p == std::string::npos || expr.find('/', p + 1) != std::string::npos) {
        error = "modulo expects exactly one \"/\" operator";
        return false;
    }
    std::string l = trim(expr.substr(0, p));
    std::string r = trim(expr.substr(p + 1));
    if (l.empty() || r.empty()) { error = "modulo: both sides must be non-empty"; return false; }
    std::string ls, rs;
    if (!evalExpression(l, ls, error)) return false;
    if (!evalExpression(r, rs, error)) return false;
    double a = std::stod(ls), b = std::stod(rs);
    if (b == 0) { error = "modulo: division by zero"; return false; }
    long long q = (long long)std::floor(a / b);
    result = std::to_string(q) + "......" + formatNumber(a - q * b);
    return true;
}

static bool evalRound(const std::string& expr, std::string& result, std::string& error) {
    std::string inner;
    if (!evalExpression(expr, inner, error)) return false;
    result = std::to_string((long long)std::round(std::stod(inner)));
    return true;
}

// Recursive-descent arithmetic evaluator.
// Grammar (precedence low→high): add/sub -> mul/div -> unary -> power(**) -> atom
// Atoms: number literal, (...), sqrt(...), factorial(n).
// "**" is right-associative (via parseUnary on the RHS).
struct EvalParser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit EvalParser(const std::string& str) : s(str) {}
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    bool parse(double& out) {
        ws();
        if (!parseAdd(out)) return false;
        ws();
        return i >= s.size();
    }
    bool parseAdd(double& out) {
        if (!parseMul(out)) return false;
        while (true) {
            ws();
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                char op = s[i++];
                double r;
                if (!parseMul(r)) return false;
                if (op == '+') out += r;
                else out -= r;
            } else break;
        }
        return true;
    }
    bool parseMul(double& out) {
        if (!parseUnary(out)) return false;
        while (true) {
            ws();
            if (i >= s.size()) break;
            if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') break;
            if (s[i] == '*' || s[i] == '/') {
                char op = s[i++];
                double r;
                if (!parseUnary(r)) return false;
                if (op == '*') out *= r;
                else {
                    if (r == 0) { err = "division by zero"; return false; }
                    out /= r;
                }
            } else break;
        }
        return true;
    }
    bool parseUnary(double& out) {
        ws();
        if (i < s.size() && s[i] == '-') { i++; double r; if (!parseUnary(r)) return false; out = -r; return true; }
        if (i < s.size() && s[i] == '+') { i++; return parseUnary(out); }
        return parsePower(out);
    }
    bool parsePower(double& out) {
        if (!parseAtom(out)) return false;
        ws();
        if (i + 1 < s.size() && s[i] == '*' && s[i + 1] == '*') {
            i += 2;
            double r;
            if (!parseUnary(r)) return false;
            out = std::pow(out, r);
        }
        return true;
    }
    bool parseAtom(double& out) {
        ws();
        if (i >= s.size()) { err = "unexpected end of expression"; return false; }
        if (s[i] == '(') {
            i++;
            if (!parseAdd(out)) return false;
            ws();
            if (i >= s.size() || s[i] != ')') { err = "missing ')'"; return false; }
            i++;
            return true;
        }
        if (i + 9 <= s.size() && s.substr(i, 9) == "Math.sqrt") {
            i += 9;
            ws();
            if (i >= s.size() || s[i] != '(') { err = "expected '(' after sqrt"; return false; }
            i++;
            double r;
            if (!parseAdd(r)) return false;
            ws();
            if (i >= s.size() || s[i] != ')') { err = "missing ')'"; return false; }
            i++;
            if (r < 0) { err = "sqrt of negative"; return false; }
            out = std::sqrt(r);
            return true;
        }
        if (i + 9 <= s.size() && s.substr(i, 9) == "factorial") {
            i += 9;
            ws();
            if (i >= s.size() || s[i] != '(') { err = "expected '(' after factorial"; return false; }
            i++;
            double r;
            if (!parseAdd(r)) return false;
            ws();
            if (i >= s.size() || s[i] != ')') { err = "missing ')'"; return false; }
            i++;
            if (r < 0 || r != std::floor(r) || r > 170) { err = "factorial: invalid argument"; return false; }
            double rr = 1;
            for (long long k = 2; k <= (long long)r; ++k) rr *= (double)k;
            out = rr;
            return true;
        }
        if (s[i] == '.' || std::isdigit((unsigned char)s[i])) {
            size_t st = i;
            while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) i++;
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                i++;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
                while (i < s.size() && std::isdigit((unsigned char)s[i])) i++;
            }
            try { out = std::stod(s.substr(st, i - st)); }
            catch (...) { err = "bad number"; return false; }
            return true;
        }
        err = "unexpected character '" + std::string(1, s[i]) + "'";
        return false;
    }
};

// Evaluates a general expression. Rules:
//  - If any text variable is present or letters exist (other than sqrt),
//    it falls back to string concatenation with "+".
//  - Otherwise, numeric: "5!" -> "factorial(5)", "^" -> "**", sqrt() supported.
static bool evalExpression(const std::string& expr, std::string& result, std::string& error) {
    std::string t = trim(expr);
    if (t.empty()) { error = "empty expression"; return false; }
    static const std::regex anyVarRe(R"RX(\[\s*"([^"]*)"\s*\])RX");
    for (auto it = std::sregex_iterator(t.begin(), t.end(), anyVarRe); it != std::sregex_iterator(); ++it) {
        std::string n = (*it)[1];
        std::string ne = varNameError(n);
        if (!ne.empty()) { error = "invalid variable reference " + it->str() + ": " + ne; return false; }
    }
    bool hasText = false, hasNum = false;
    for (auto it = std::sregex_iterator(t.begin(), t.end(), anyVarRe); it != std::sregex_iterator(); ++it) {
        std::string n = (*it)[1];
        if (!variables.count(n)) { error = "variable \"" + n + "\" not found"; return false; }
        if (variables[n].isText) hasText = true;
        else hasNum = true;
    }
    if (hasText && hasNum) { error = "cannot mix text and number variables"; return false; }
    std::string stripped = std::regex_replace(t, anyVarRe, "");
    std::string noSqrt;
    {
        size_t i = 0;
        while (i < stripped.size()) {
            if (i + 4 <= stripped.size() && toLower(stripped.substr(i, 4)) == "sqrt") { i += 4; continue; }
            noSqrt += stripped[i++];
        }
    }
    bool hasLetters = false;
    for (char c : noSqrt) if (std::isalpha((unsigned char)c)) { hasLetters = true; break; }
    if (hasText || hasLetters) {
        if (stripped.find_first_of("-*/^!") != std::string::npos) { error = "text expressions only support \"+\""; return false; }
        std::vector<std::string> parts;
        std::string cur;
        for (char c : t) {
            if (c == '+') { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        parts.push_back(cur);
        result.clear();
        static const std::regex varRe(R"RX(\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\])RX");
        static const std::regex pureVarRe(R"RX(^\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\]$)RX");
        for (auto& part : parts) {
            std::string p = trim(part);
            if (p.empty()) continue;
            std::smatch pm;
            if (std::regex_match(p, pm, pureVarRe)) {
                std::string n = pm[1];
                if (!variables.count(n)) { error = "variable \"" + n + "\" not found"; return false; }
                if (!variables[n].isText) { error = "variable \"" + n + "\" is not text"; return false; }
                result += variables[n].str;
                continue;
            }
            size_t pos = 0;
            bool bad = false;
            while (pos < p.size()) {
                std::smatch m;
                std::string sub = p.substr(pos);
                if (std::regex_search(sub, m, varRe)) {
                    size_t st = m.position(0);
                    result += sub.substr(0, st);
                    std::string n = m[1];
                    if (!variables.count(n)) { error = "variable \"" + n + "\" not found"; bad = true; break; }
                    if (!variables[n].isText) { error = "variable \"" + n + "\" is not text"; bad = true; break; }
                    result += variables[n].str;
                    pos += st + m.length(0);
                } else {
                    result += sub;
                    break;
                }
            }
            if (bad) return false;
        }
        return true;
    }
    // Numeric path: inline variable values, then transform for the parser.
    std::string evalExpr;
    {
        static const std::regex varRe(R"RX(\[\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\])RX");
        size_t pos = 0;
        while (pos < t.size()) {
            std::smatch m;
            std::string sub = t.substr(pos);
            if (std::regex_search(sub, m, varRe)) {
                evalExpr += sub.substr(0, m.position(0));
                std::string n = m[1];
                if (!variables.count(n) || variables[n].isText) evalExpr += "NaN";
                else evalExpr += formatNumber(variables[n].num);
                pos += m.position(0) + m.length(0);
            } else {
                evalExpr += sub;
                break;
            }
        }
    }
    {
        static const std::regex re(R"RX(\.\d+!)RX");
        if (std::regex_search(evalExpr, re)) { error = "factorial \"!\" requires an integer"; return false; }
    }
    {
        std::regex re(R"RX((\d+)!)RX");
        evalExpr = std::regex_replace(evalExpr, re, "factorial($1)");
    }
    {
        std::string tmp;
        for (char c : evalExpr) {
            if (c == '^') tmp += "**";
            else tmp += c;
        }
        evalExpr = tmp;
    }
    {
        std::string noMath;
        size_t i = 0;
        while (i < evalExpr.size()) {
            if (i + 9 <= evalExpr.size() && evalExpr.substr(i, 9) == "Math.sqrt") { i += 9; continue; }
            if (i + 9 <= evalExpr.size() && evalExpr.substr(i, 9) == "factorial") { i += 9; continue; }
            noMath += evalExpr[i++];
        }
        for (char c : noMath)
            if (std::isalpha((unsigned char)c)) { error = std::string("unknown identifier \"") + c + "\""; return false; }
    }
    if (evalExpr.find_first_of("&|<>?:") != std::string::npos) { error = "unsupported operator"; return false; }
    EvalParser ep(evalExpr);
    double r;
    if (!ep.parse(r)) { error = "cannot evaluate expression: " + ep.err; return false; }
    if (std::isnan(r)) { error = "expression evaluated to NaN"; return false; }
    if (!std::isfinite(r)) { error = "expression evaluated to Infinity"; return false; }
    result = formatNumber(r);
    return true;
}

struct CompExprResult {
    bool ok{};
    std::string expr;
    std::string remaining;
    std::string error;
};

// Reads one computation operand: "literal" or ["var"]. wrapLit=true wraps literals in parens.
static bool readCompOperand(const std::string& str, bool wrapLit, std::string& outExpr, std::string& rem) {
    rem = ltrim(str);
    if (rem.empty()) return false;
    if (rem[0] == '"') {
        size_t j = 1;
        while (j < rem.size() && rem[j] != '"') j++;
        if (j >= rem.size()) return false;
        std::string inner = rem.substr(1, j - 1);
        outExpr = wrapLit ? "(" + inner + ")" : inner;
        rem = rem.substr(j + 1);
        return true;
    }
    static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
    std::smatch m;
    if (std::regex_search(rem, m, varRe)) {
        outExpr = "[\"" + m[1].str() + "\"]";
        rem = rem.substr(m.length());
        return true;
    }
    return false;
}

// Parses a chain of operands separated by + - * / ^ ** until a suffix
// (-["var"] store target or -color["c"]) is reached. Suffixes are left in `remaining`.
static CompExprResult parseComputationExpr(const std::string& str) {
    CompExprResult res{};
    std::string rem = ltrim(str);
    if (rem.empty()) { res.error = "empty expression"; return res; }
    std::string expr, r2;
    if (!readCompOperand(rem, false, expr, r2)) { res.error = "expected operand"; return res; }
    rem = r2;
    while (true) {
        rem = ltrim(rem);
        if (rem.empty()) break;
        static const std::regex sfx1(R"RX(^-\s*\[\s*"[^"]+"\s*\]\s*$)RX");
        static const std::regex sfx2(R"RX(^-\s*\[\s*"[^"]+"\s*\]\s*-\s*color\s*\[)RX", std::regex::icase);
        static const std::regex sfx3(R"RX(^-\s*color\s*\[)RX", std::regex::icase);
        if (std::regex_search(rem, sfx1)) break;
        if (std::regex_search(rem, sfx2)) break;
        if (std::regex_search(rem, sfx3)) break;
        static const std::regex opRe(R"RX(^(\+|\-|\*\*|\*|\/|\^))RX");
        std::smatch m;
        if (!std::regex_search(rem, m, opRe)) break;
        std::string op = m[1];
        rem = rem.substr(m.length());
        std::string nextExpr;
        if (!readCompOperand(rem, true, nextExpr, r2)) { res.error = "expected operand after \"" + op + "\""; return res; }
        expr += " " + op + " " + nextExpr;
        rem = r2;
    }
    res.ok = true;
    res.expr = expr;
    res.remaining = rem;
    return res;
}

struct Ctx {
    bool skipDisplayCommand{};
    std::string name;
};

using Handler = std::function<std::string(const std::string&, const Ctx&)>;

static std::map<std::string, Handler> COMMANDS;
// Each entry: { "flag" | "branch", regex }. "flag" consumes just the matched token.
// "branch" also consumes the balanced (...) that follows.
static std::map<std::string, std::vector<std::pair<std::string, std::string>>> CONT;
static std::regex g_cmdRegex;
static bool g_cmdRegexValid{};

static std::string processCommand(const std::string& input, bool skipDisplay = false);
static void executeSequentialCommands(const std::vector<std::string>& subcommands);

static void scheduleDelayedCommand(int delaySeconds, const std::string& cmd) {
    std::thread([delaySeconds, cmd]() {
        std::this_thread::sleep_for(std::chrono::seconds(delaySeconds));
        std::string out = processCommand(cmd, true);
        if (!out.empty()) rawOut(out + "\n");
    }).detach();
}

static void defineCommand(const std::string& name, Handler h, std::vector<std::pair<std::string, std::string>> conts = {}) {
    std::string l = toLower(name);
    COMMANDS[l] = h;
    CONT[l] = conts;
    g_cmdRegexValid = false;
}

// Builds a single regex `^(cmd1|cmd2|...)` ordered longest-first, so
// "variable-delete" is preferred over "variable".
static void buildCommandRegex() {
    std::vector<std::string> names;
    for (auto& p : COMMANDS) names.push_back(p.first);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    std::string pat = "^(";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) pat += "|";
        for (char c : names[i]) {
            if (std::string(".^$|()[]{}*+?\\").find(c) != std::string::npos) pat += '\\';
            pat += c;
        }
    }
    pat += ")(?![A-Za-z0-9_])\\s*([\\s\\S]*)$";
    g_cmdRegex = std::regex(pat, std::regex::icase);
    g_cmdRegexValid = true;
}

static std::string getLeadingCommandName(const std::string& content) {
    static const std::regex re(R"RX(^([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*))RX");
    std::smatch m;
    if (std::regex_search(content, m, re)) return toLower(m[1]);
    return "";
}

// Splits `(a)-(b)-(c)` into ["a","b","c"].
// After each sub-command, if the command name has continuation rules, they are
// attached to that sub-command so `-true=(...)`/`-color[...]` are not split off.
static std::vector<std::string> extractSubCommands(const std::string& str) {
    std::vector<std::string> subs;
    size_t i = 0;
    const size_t len = str.size();
    while (i < len) {
        while (i < len && (str[i] == ' ' || str[i] == '\t' || str[i] == '-')) i++;
        if (i >= len) break;
        if (str[i] != '(') return {};
        int depth = 0;
        bool inStr = false;
        size_t start = i;
        for (; i < len; ++i) {
            char c = str[i];
            if (inStr) {
                if (c == '"') inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; continue; }
            if (c == '(') depth++;
            else if (c == ')') {
                depth--;
                if (depth == 0) {
                    subs.push_back(trim(str.substr(start + 1, i - start - 1)));
                    i++;
                    break;
                }
            }
        }
        if (depth != 0) return {};
        if (subs.empty()) continue;
        std::string cmdName = getLeadingCommandName(subs.back());
        auto it = CONT.find(cmdName);
        if (it == CONT.end() || it->second.empty()) continue;
        while (i < len) {
            while (i < len && (str[i] == ' ' || str[i] == '\t')) i++;
            if (i >= len) break;
            if (str[i] != '-') break;
            std::string cont = str.substr(i);
            bool matched = false;
            for (auto& rule : it->second) {
                std::regex re(rule.second, std::regex::icase);
                std::smatch m;
                if (!std::regex_search(cont, m, re)) continue;
                if (rule.first == "branch") {
                    auto p = extractParenthesized(cont, m.length() - 1);
                    if (!p.found) return {};
                    std::string seg = cont.substr(0, m.length() - 1) + "(" + p.content + ")";
                    subs.back() += seg;
                    i += seg.size();
                    matched = true;
                    break;
                } else {
                    subs.back() += cont.substr(0, m.length());
                    i += m.length();
                    matched = true;
                    break;
                }
            }
            if (!matched) break;
        }
    }
    return subs;
}

// A pending wait; polled every 100ms by a background thread.
struct Wait {
    std::string varName;
    std::string matchType;   // "" | "text" | "number"
    std::string matchValue;  // "" if not requiring a specific value
    std::string cmd;
    std::string id;          // "" means unnumbered
    bool hasMatchValue{};
    int delaySeconds{};      // wait-time "N"
    std::atomic<bool> cancelled{false};
    std::atomic<bool> fired{false};
};

static std::vector<std::shared_ptr<Wait>> activeWaits;
static std::mutex activeWaitsMutex;

static bool checkWaitCondition(const Wait& w) {
    auto it = variables.find(w.varName);
    if (it == variables.end()) return false;
    const Variable& v = it->second;
    if (!w.matchType.empty()) {
        if (w.matchType == "text" && !v.isText) return false;
        if (w.matchType == "number" && v.isText) return false;
    }
    if (!w.hasMatchValue) return true;
    if (!v.isText) {
        if (!isValidNumber(w.matchValue)) return false;
        return v.num == std::stod(w.matchValue);
    }
    return v.str == w.matchValue;
}

static void fireWait(std::shared_ptr<Wait> w) {
    if (w->delaySeconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(w->delaySeconds));
        if (w->cancelled) return;
    }
    std::string out;
    try { out = processCommand(w->cmd, true); }
    catch (...) { out = err("wait: internal error"); }
    if (!out.empty()) rawOut(out + "\n");
}

static bool tryFireWait(std::shared_ptr<Wait> w) {
    if (w->cancelled) return false;
    if (!checkWaitCondition(*w)) return false;
    w->cancelled = true;
    w->fired = true;
    {
        std::lock_guard<std::mutex> lk(activeWaitsMutex);
        activeWaits.erase(std::remove(activeWaits.begin(), activeWaits.end(), w), activeWaits.end());
    }
    std::thread([w]() { fireWait(w); }).detach();
    return true;
}

static void startWait(std::shared_ptr<Wait> w) {
    if (tryFireWait(w)) return;
    {
        std::lock_guard<std::mutex> lk(activeWaitsMutex);
        activeWaits.push_back(w);
    }
    std::thread([w]() {
        while (!w->cancelled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (w->cancelled) return;
            tryFireWait(w);
            if (w->fired) return;
        }
    }).detach();
}

// wait / wait-time / wait[appear]:
//   wait["v"]="x"-(cmd)
//   wait["v"]=text"x"-(cmd)
//   wait[appear]-["v"]=number-(cmd)
// Optional trailing -"id" or -["var"] names this wait for wait-cancel.
static std::string waitImpl(const std::string& rest, int forcedDelay) {
    std::string rem = trim(rest);
    int delaySeconds = forcedDelay;
    std::smatch m;
    if (delaySeconds == 0) {
        static const std::regex wtm(R"RX(^time\s*"(\d+)"\s*-\s*)RX", std::regex::icase);
        if (std::regex_search(rem, m, wtm)) {
            delaySeconds = std::stoi(m[1]);
            rem = trim(rem.substr(m.length()));
        }
    }
    static const std::regex appearRe(R"RX(^\[\s*appear\s*\]\s*)RX", std::regex::icase);
    if (std::regex_search(rem, m, appearRe)) rem = trim(rem.substr(m.length()));
    if (!rem.empty() && rem[0] == '-') rem = trim(rem.substr(1));
    static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
    if (!std::regex_search(rem, m, varRe)) return err("wait: expected [\"varName\"]");
    std::string varName = m[1];
    rem = trim(rem.substr(m.length()));
    std::string matchType;
    bool hasMatchValue = false;
    std::string matchValue;
    static const std::regex eqRe(R"RX(^=\s*)RX");
    if (std::regex_search(rem, m, eqRe)) {
        rem = trim(rem.substr(m.length()));
        static const std::regex typeRe(R"RX(^(text|number)\b\s*)RX", std::regex::icase);
        if (std::regex_search(rem, m, typeRe)) {
            matchType = toLower(m[1]);
            rem = trim(rem.substr(m.length()));
        }
        if (!rem.empty() && rem[0] == '"') {
            size_t j = 1;
            std::string val;
            while (j < rem.size() && rem[j] != '"') {
                val += rem[j];
                j++;
            }
            if (j >= rem.size()) return err("wait: unterminated value string");
            matchValue = val;
            hasMatchValue = true;
            rem = trim(rem.substr(j + 1));
        }
    }
    if (rem.empty() || rem[0] != '-') return err("wait: expected '-' before (command)");
    rem = trim(rem.substr(1));
    auto p = extractParenthesized(rem, 0);
    if (!p.found) return err("wait: expected (command)");
    std::string cmd = p.content;
    rem = trim(rem.substr(p.endPos));
    std::string id;
    if (!rem.empty()) {
        if (rem[0] != '-') return err("wait: unexpected trailing text");
        rem = trim(rem.substr(1));
        if (!rem.empty() && rem[0] == '=') rem = trim(rem.substr(1));
        if (!rem.empty() && rem[0] == '"') {
            size_t j = 1;
            while (j < rem.size() && rem[j] != '"') {
                id += rem[j];
                j++;
            }
            if (j >= rem.size()) return err("wait: unterminated id string");
            rem = trim(rem.substr(j + 1));
        } else if (!rem.empty() && rem[0] == '[') {
            static const std::regex idVarRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
            std::smatch m2;
            if (!std::regex_search(rem, m2, idVarRe)) return err("wait: bad id variable reference");
            auto rv = readVarValue(m2[1], "wait");
            if (!rv.ok) return err(rv.error);
            id = rv.text;
            rem = trim(rem.substr(m2.length()));
        } else return err("wait: expected id");
        if (!rem.empty()) return err("wait: unexpected trailing text");
    }
    std::string ne = varNameError(varName);
    if (!ne.empty()) return err("wait: " + ne);
    auto w = std::make_shared<Wait>();
    w->varName = varName;
    w->matchType = matchType;
    w->hasMatchValue = hasMatchValue;
    w->matchValue = matchValue;
    w->delaySeconds = delaySeconds;
    w->cmd = cmd;
    w->id = id;
    startWait(w);
    return "";
}

// Blocking key read with timeout. -1 = wait forever.
static int readKeyWithTimeout(int timeoutMs) {
#ifdef _WIN32
    if (timeoutMs < 0) return _getch();
    auto st = std::chrono::steady_clock::now();
    while (true) {
        if (_kbhit()) return _getch();
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - st).count();
        if (el >= timeoutMs) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int c = -1;
    if (timeoutMs < 0) {
        c = getchar();
    } else {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int r = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (r > 0) c = getchar();
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return c;
#endif
}

#ifdef _WIN32
static void enableAnsi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
static void enableAnsi() {}
#endif

// Reads one logical key. Returned tokens:
//   "\n"        = Enter -> submit the block
//   "INSERT_NL" = Ctrl+L -> insert a newline inside the block (L = Line)
//   "CTRL_C"    = quit
//   "ESC"       = clear current block
//   "BACKSPACE","DEL","LEFT","RIGHT","HOME","END","UP","DOWN","PGUP","PGDN"
//   single printable char
static std::string readKey() {
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 224) {
        int c2 = _getch();
        switch (c2) {
        case 72: return "UP";
        case 80: return "DOWN";
        case 75: return "LEFT";
        case 77: return "RIGHT";
        case 71: return "HOME";
        case 79: return "END";
        case 73: return "PGUP";
        case 81: return "PGDN";
        case 83: return "DEL";
        }
        return "";
    }
    if (c == 3) return "CTRL_C";
    if (c == 16) return "CTRL_P";
    if (c == 14) return "CTRL_N";
    if (c == 8) return "BACKSPACE";
    if (c == 12) return "INSERT_NL";
    if (c == 13) return "\n";
    if (c == 27) return "ESC";
    char buf[2] = { (char)c, 0 };
    return buf;
#else
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return "";
    if (c == 3) return "CTRL_C";
    if (c == 16) return "CTRL_P";
    if (c == 14) return "CTRL_N";
    if (c == 8 || c == 127) return "BACKSPACE";
    if (c == 12) return "INSERT_NL";
    if (c == 13) return "\n";
    if (c != 27) {
        char buf[2] = { (char)c, 0 };
        return buf;
    }
    struct timeval tv{0, 50000};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return "ESC";
    unsigned char seq[8];
    int n = 0;
    while (n < 6) {
        unsigned char b;
        if (read(STDIN_FILENO, &b, 1) != 1) break;
        seq[n++] = b;
        if ((b >= 'A' && b <= 'Z') || b == '~') break;
    }
    if (n == 0) return "ESC";
    if (seq[0] == '[' && n >= 2) {
        if (seq[1] == 'A') return "UP";
        if (seq[1] == 'B') return "DOWN";
        if (seq[1] == 'C') return "RIGHT";
        if (seq[1] == 'D') return "LEFT";
        if (seq[1] == 'H') return "HOME";
        if (seq[1] == 'F') return "END";
        if (n >= 3 && seq[1] == '3' && seq[2] == '~') return "DEL";
        if (n >= 3 && seq[1] == '5' && seq[2] == '~') return "PGUP";
        if (n >= 3 && seq[1] == '6' && seq[2] == '~') return "PGDN";
        if (n >= 3 && seq[1] == '1' && seq[2] == '~') return "HOME";
        if (n >= 3 && seq[1] == '4' && seq[2] == '~') return "END";
    }
    if (seq[0] == 'O' && n >= 2) {
        if (seq[1] == 'A') return "UP";
        if (seq[1] == 'B') return "DOWN";
        if (seq[1] == 'C') return "RIGHT";
        if (seq[1] == 'D') return "LEFT";
        if (seq[1] == 'H') return "HOME";
        if (seq[1] == 'F') return "END";
    }
    return "ESC";
#endif
}

// RAII: put stdin into raw mode (no echo, no line buffering) while a block is edited.
struct RawMode {
    bool active{};
#ifndef _WIN32
    struct termios oldt;
    void on() {
        tcgetattr(STDIN_FILENO, &oldt);
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        active = true;
    }
    void off() {
        if (active) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            active = false;
        }
    }
    ~RawMode() { off(); }
#else
    void on() { active = true; }
    void off() { active = false; }
    ~RawMode() {}
#endif
};

// Measure how long the user holds `key` after the initial press.
// - Repeats of `key` = "still holding" and refresh the on-screen progress.
// - Quiet gap >= releaseMs after any repeat = "released"; returns held seconds.
// - No repeat within tapGraceMs = quick tap -> returns 0.
// - Different char = cancelled -> returns -1.
// - Holding to maxSeconds = returns maxSeconds.
// When displayPrefix is non-empty, the current state is rendered in-place as
//   "<prefix>[<key>] <elapsed>s"
// on every refresh, so the user can see the held key inside the [] box.
static int measureHoldDuration(const std::string& key, int maxSeconds,
                               const std::string& displayPrefix = "") {
    auto start = std::chrono::steady_clock::now();
    auto lastEvent = start;
    bool sawRepeat = false;
    int lastDisplayMs = -1;
    const int maxMs = maxSeconds * 1000;
    const int releaseMs = 300;
    const int tapGraceMs = 800;

    auto renderDisplay = [&](int heldMs) {
        if (displayPrefix.empty()) return;
        char buf[256];
        if (heldMs < 0)
            std::snprintf(buf, sizeof(buf), "\r\033[K%s[%s]", displayPrefix.c_str(), key.c_str());
        else
            std::snprintf(buf, sizeof(buf), "\r\033[K%s[%s] %.1fs",
                          displayPrefix.c_str(), key.c_str(), heldMs / 1000.0);
        rawOut(buf);
    };

    // Draw immediately on entry so the pressed key appears inside the box.
    renderDisplay(-1);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed >= maxMs) {
            renderDisplay(maxMs);
            return maxSeconds;
        }
        int c = readKeyWithTimeout(30);
        now = std::chrono::steady_clock::now();
        int heldMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        if (!displayPrefix.empty() && heldMs - lastDisplayMs >= 50) {
            renderDisplay(heldMs);
            lastDisplayMs = heldMs;
        }

        if (c >= 0) {
            if (std::string(1, (char)c) == key) {
                lastEvent = now;
                sawRepeat = true;
            } else {
                return -1;
            }
        } else {
            int sinceLast = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEvent).count();
            if (sawRepeat) {
                if (sinceLast >= releaseMs) {
                    int realHeldMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(lastEvent - start).count();
                    renderDisplay(realHeldMs);
                    return (realHeldMs + 100) / 1000;
                }
            } else {
                if (sinceLast >= tapGraceMs) {
                    return 0;
                }
            }
        }
    }
}

static std::string helpText;

static void initHelpText() {
    helpText = R"HELP(=== CompactScript Command Syntax Reference ===

[Variable Creation]
text["name"]"value"
number["name"]"123"
text["name"]-color["color"]-"value"
number["name"]-color["color"]-"123"
Color/newline tokens: -n- -nr- -r- -color["c"]

[Variable Access]
text["name"]
number["name"]

[Multi-Color Output]
text-color["c1"]"s1"-color["c2"]"s2"
text-color["c1"]"s1"-n-color["c2"]"s2"
number-color["c1"]"s1"-color["c2"]"s2"

[Variable Type Conversion]
variable-type["name"]-number
variable-type["name"]-text

[Variable Color]
variable-color["name"]-color["color"]

[Concatenation]
text[concatenation]-"expr"
number[concatenation]-"expr"

[List]
list["n"]-"i1"(c1)-"i2"(c2)-...
list["n"]
list["n"]"item"
list["n"]=["var"]
list
list-add["n"]-"item"(cmd)
list-add["n"]-["var"](cmd)
list-add=["n"]-"item"(cmd)
list-delete["n"]"item"
list-delete["n"]=["var"]
list-delete["n"]=all
list-delete =all
list-rename["old"]"new"
list-rename["old"]=["var"]
list-rename=["var"]"new"
list-rename=["v1"]=["v2"]
list-replace["n"]"old"-"new"(cmd)
list-replace["n"]-["v"]-"new"(cmd)
list-replace["n"]-"old"-["v"](cmd)
list-replace["n"]-["v1"]-["v2"]=(cmd)
list-replace=["n"]"old"-"new"(cmd)

[Color System]
red, light_red, dark_red, green, light_green, dark_green,
blue, light_blue, dark_blue, yellow, light_yellow, dark_yellow,
cyan, light_cyan, dark_cyan, magenta, light_magenta, dark_magenta,
orange, light_orange, dark_orange, purple, light_purple, dark_purple

[Variable Management]
variable
variable-delete "name"
variable-delete =all
variable-rename["old"]"new"
variable-replace["name"]"new"
variable-exist["name"]-true=(cmd)-false=(cmd)
variable-exist=["var"]-true=(cmd)-false=(cmd)

[Exist]
-true=(cmd)
-false=(cmd)
(null) to skip a branch
list-exist["name"]-true=(cmd)-false=(cmd)
list-exist["name"]-"item"-true=(cmd)-false=(cmd)
functional-exist["name"]-true=(cmd)-false=(cmd)
functional-exist["name"]-N"cmd"-true=(cmd)-false=(cmd)

[Wait]
wait["var"]="value"-(cmd)
wait["var"]=text"value"-(cmd)
wait["var"]=number"123"-(cmd)
wait["var"]-(cmd)
wait[appear]-["var"]-(cmd)
wait[appear]-["var"]=text-(cmd)
wait[appear]-["var"]=number-(cmd)
wait[appear]-["var"]=text"v"-(cmd)
wait[appear]-["var"]=number"n"-(cmd)
wait-time"N"-["var"]="value"-(cmd)
time"N"-wait["var"]="value"-(cmd)
Optional id: wait["a"]="v"-(cmd)-"id"

[Wait Cancel]
wait-cancel=all
wait-cancel=numbered
wait-cancel=unnumbered
wait-cancel"id"
wait-cancel=["var"]

[Input]
input["var"]-text"prompt"
input["var"]-number"prompt"
input["var"]-color["c"]-text"prompt"
input["var"]-text"prompt"-color["c"]
input-choose["var"]-text"prompt"-"opt"="val"...
input-choose"prompt"-"opt"=(cmd)...
input-choose[long-press]-["var"]-type"prompt"-time["N"]"key"="value"...
input-choose[long-press]"prompt"-time["N"]"key"=(cmd)...

[String]
length"text"
length["varName"]
length[list]-["name"]
length[list]-["name"]"item"
length[functional]-["name"]
length[functional]-["name"]-N

[Computation]
computation"1"+"1"
computation"sqrt(9)"
computation"5!"
computation["a"]+"1"
computation"1"-"1"
computation"1"+"1"-color["red"]
computation[modulo]"12"/"5"
computation[round]"7.2"
computation[variable]"1"+"1"-["var"]
computation[variable]-["a"]+"1"-["var"]
computation[variable-modulo]"12"/"5"-["var"]
computation[variable-round]"7.2"-["var"]

[Conditional - if]
if<cond>-true=(cmd)-false=(cmd)
Numbers compare numerically, texts compare length.

[While Loop]
while<cond>-do(cmd1)-(cmd2)-...
while<cond>-do=(cmd1)-(cmd2)-...

[Function]
functional
functional["name"]-(cmd1)-(cmd2)-...
functional["name"]
functional=["var"]
functional-rename["old"]"new"
functional-delete "name"
functional-delete=["var"]
functional-delete =all
functional-replace["name"]-(cmd1)-(cmd2)-...

[Save & Load]
save"name"
save"name"-xor
load

[System & Utility]
clear
help
time"seconds"-command
time["varName"]-command
repeat"n"-command
repeat["varName"]-command
random-number"1"-"10"
random-text"a"-"b"
random(cmd1)-(cmd2)-...
random"N"-(cmd1)-(cmd2)-...
random"N"-text"a"-"b"-"c"
random"N"-number"1"-"5"
random[shuffle]-text"a"-"b"-"c"
random[shuffle]-number"1"-"2"-"3"
random[shuffle]-(c1)-(c2)-...
random[shuffle]-["a"]-["b"]-...
random -uuid
run(cmd1)-(cmd2)-...

[Random with variables]
random["a"]-["b"]
random["c"]-["d"]-["e"]...
random[shuffle]-["a"]-["b"]-["c"]...

[Comments]
#"..." only counts as a comment at START of line

[Keyboard]
Enter            = submit the WHOLE block
Ctrl+L           = insert a newline (L = Line)
Ctrl+P / Ctrl+N  = scroll screen up / down 3 lines
Up / Down        = command history (previous / next)
PgUp / PgDn      = scroll screen 15 lines
Left / Right     = move cursor
Home / End       = jump to start / end
Backspace / Del  = delete (Backspace at line start pulls the previous line back)
Esc              = clear the whole current block
Ctrl+C           = quit

)HELP";
}

static void setupCommands() {
    defineCommand("clear", [](const std::string&, const Ctx&) -> std::string { rawOut("\033[2J\033[H"); return ""; });

    // Easter egg: 23 June = Turing's birthday.
    defineCommand("help", [](const std::string&, const Ctx&) -> std::string {
        time_t t = std::time(nullptr);
        struct tm* lt = localtime(&t);
        std::string b = helpText;
        if (lt && lt->tm_mon == 5 && lt->tm_mday == 23) b += "\n(happy birthday, Alan Turing)";
        return b;
    });

    defineCommand("wait", [](const std::string& rest, const Ctx&) -> std::string { return waitImpl(rest, 0); });

    defineCommand("wait-time", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^"(\d+)"\s*-\s*([\s\S]*)$)RX");
        if (!std::regex_search(rest, m, re)) return err("wait-time: Usage: wait-time\"N\"-[\"var\"]=\"value\"-(command)");
        int d = std::stoi(m[1]);
        if (d < 0) return err("wait-time: delay must be non-negative");
        return waitImpl(m[2], d);
    });

    // Cancels pending waits by scope or by id (literal or from a text variable).
    defineCommand("wait-cancel", [](const std::string& rest, const Ctx&) -> std::string {
        std::string t = trim(rest);
        if (t.empty()) return err("wait-cancel: bad syntax");
        if (std::regex_match(t, std::regex(R"RX(^=\s*all\s*$)RX", std::regex::icase))) {
            std::lock_guard<std::mutex> lk(activeWaitsMutex);
            for (auto& w : activeWaits) w->cancelled = true;
            activeWaits.clear();
            return "";
        }
        if (std::regex_match(t, std::regex(R"RX(^=\s*numbered\s*$)RX", std::regex::icase))) {
            std::lock_guard<std::mutex> lk(activeWaitsMutex);
            std::vector<std::shared_ptr<Wait>> keep;
            for (auto& w : activeWaits) {
                if (!w->id.empty()) w->cancelled = true;
                else keep.push_back(w);
            }
            activeWaits.swap(keep);
            return "";
        }
        if (std::regex_match(t, std::regex(R"RX(^=\s*unnumbered\s*$)RX", std::regex::icase))) {
            std::lock_guard<std::mutex> lk(activeWaitsMutex);
            std::vector<std::shared_ptr<Wait>> keep;
            for (auto& w : activeWaits) {
                if (w->id.empty()) w->cancelled = true;
                else keep.push_back(w);
            }
            activeWaits.swap(keep);
            return "";
        }
        std::string targetId;
        if (!t.empty() && t[0] == '"') {
            size_t j = 1;
            while (j < t.size() && t[j] != '"') {
                targetId += t[j];
                j++;
            }
            if (j >= t.size()) return err("wait-cancel: unterminated id string");
            std::string tail = trim(t.substr(j + 1));
            if (!tail.empty()) return err("wait-cancel: unexpected trailing text");
        } else {
            std::smatch m;
            static const std::regex vRe(R"RX(^=?\s*\[\s*"([^"]+)"\s*\]\s*$)RX");
            if (!std::regex_search(t, m, vRe)) return err("wait-cancel: bad syntax");
            auto rv = readVarValue(m[1], "wait-cancel");
            if (!rv.ok) return err(rv.error);
            targetId = rv.text;
        }
        std::lock_guard<std::mutex> lk(activeWaitsMutex);
        std::vector<std::shared_ptr<Wait>> keep;
        for (auto& w : activeWaits) {
            if (w->id == targetId) w->cancelled = true;
            else keep.push_back(w);
        }
        activeWaits.swap(keep);
        return "";
    });

    // random-number"min"-"max": inclusive integer range.
    defineCommand("random-number", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^"(-?\d+)"\s*-\s*"(-?\d+)"\s*$)RX");
        if (!std::regex_search(rest, m, re)) return err("random-number: Usage: random-number\"min\"-\"max\"");
        int mn = std::stoi(m[1]), mx = std::stoi(m[2]);
        if (mn > mx) return err("random-number: lower > upper");
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> d(mn, mx);
        return std::to_string(d(rng));
    });

    defineCommand("random-text", [](const std::string& rest, const Ctx&) -> std::string {
        std::vector<std::string> params;
        static const std::regex re(R"RX("([^"]*)")RX");
        for (auto it = std::sregex_iterator(rest.begin(), rest.end(), re); it != std::sregex_iterator(); ++it) params.push_back((*it)[1]);
        if (params.size() < 2) return err("random-text: need at least 2 quoted args");
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<size_t> d(0, params.size() - 1);
        return params[d(rng)];
    });

    defineCommand("variable-color", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*color\s*\[\s*"([^"]+)"\s*\]\s*$)RX", std::regex::icase);
        if (!std::regex_search(rest, m, re)) return err("variable-color: Usage: variable-color[\"name\"]-color[\"colorName\"]");
        std::string ne = varNameError(m[1]);
        if (!ne.empty()) return err("variable-color: " + ne);
        std::string ce = colorError(m[2]);
        if (!ce.empty()) return err("variable-color: " + ce);
        if (!variables.count(m[1])) return err("variable-color: variable \"" + m[1].str() + "\" not found");
        variables[m[1]].color = m[2];
        variables[m[1]].segments.clear();
        return "";
    });

    // variable-type["name"]-number|text. Setting a text var to number requires the whole string to parse.
    defineCommand("variable-type", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*(text|number)\s*$)RX", std::regex::icase);
        if (!std::regex_search(rest, m, re)) return err("variable-type: Usage: variable-type[\"name\"]-text|number");
        std::string ne = varNameError(m[1]);
        if (!ne.empty()) return err("variable-type: " + ne);
        if (!variables.count(m[1])) return err("variable-type: variable \"" + m[1].str() + "\" not found");
        Variable& v = variables[m[1]];
        std::string target = toLower(m[2]);
        if ((v.isText && target == "text") || (!v.isText && target == "number")) return "";
        if (target == "number") {
            if (!isValidNumber(trim(v.str))) return err("variable-type: cannot convert to number");
            Variable nv;
            nv.isText = false;
            nv.num = std::stod(trim(v.str));
            nv.color = v.color;
            nv.segments = v.segments;
            variables[m[1]] = nv;
        } else {
            std::string s = v.isText ? v.str : formatNumber(v.num);
            Variable nv;
            nv.isText = true;
            nv.str = s;
            nv.color = v.color;
            nv.segments = v.segments;
            variables[m[1]] = nv;
        }
        return "";
    });

    defineCommand("variable", [](const std::string& rest, const Ctx&) -> std::string {
        if (!trim(rest).empty()) return err("variable: takes no arguments");
        if (variables.empty()) return hint("(no variables defined)");
        std::string out;
        for (auto& p : variables) {
            out += p.second.isText ? "text: " : "number: ";
            out += p.first + " = " + p.second.toString();
            if (!p.second.color.empty()) out += "  [color: " + p.second.color + "]";
            out += "\n";
        }
        if (!out.empty() && out.back() == '\n') out.pop_back();
        return out;
    });

    defineCommand("variable-delete", [](const std::string& rest, const Ctx&) -> std::string {
        std::string p = trim(rest);
        if (p.empty()) return err("variable-delete: Usage: variable-delete \"name\" or =all");
        if (toLower(p) == "=all") { variables.clear(); return ""; }
        if (p[0] == '=') p = trim(p.substr(1));
        if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
        std::string ne = varNameError(p);
        if (!ne.empty()) return err("variable-delete: " + ne);
        if (!variables.count(p)) return err("variable-delete: variable \"" + p + "\" not found");
        variables.erase(p);
        return "";
    });

    defineCommand("variable-rename", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*"([^"]*)"\s*$)RX");
        if (!std::regex_search(rest, m, re)) return err("variable-rename: Usage: variable-rename[\"old\"]\"new\"");
        std::string e1 = varNameError(m[1]);
        if (!e1.empty()) return err("variable-rename: " + e1);
        std::string e2 = varNameError(m[2]);
        if (!e2.empty()) return err("variable-rename: " + e2);
        if (!variables.count(m[1])) return err("variable-rename: variable \"" + m[1].str() + "\" not found");
        if (variables.count(m[2])) return err("variable-rename: variable \"" + m[2].str() + "\" already exists");
        variables[m[2]] = variables[m[1]];
        variables.erase(m[1]);
        return "";
    });

    // Replaces a variable's content while preserving its current type.
    // If a number variable receives a newline-containing value, it becomes text.
    defineCommand("variable-replace", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*([\s\S]+)$)RX");
        if (!std::regex_search(rest, m, re)) return err("variable-replace: Usage: variable-replace[\"name\"]\"newValue\"");
        std::string ne = varNameError(m[1]);
        if (!ne.empty()) return err("variable-replace: " + ne);
        if (!variables.count(m[1])) return err("variable-replace: variable \"" + m[1].str() + "\" not found");
        std::vector<Segment> segs;
        std::string pe;
        if (!parseMultiColorValue(trim(m[2]), "", true, segs, pe)) return err("variable-replace: " + pe);
        std::string parsed;
        for (auto& s : segs) parsed += s.text;
        Variable& v = variables[m[1]];
        if (v.isText) {
            v.str = parsed;
            v.segments.clear();
        } else {
            if (parsed.find('\n') != std::string::npos) {
                v.isText = true;
                v.str = parsed;
                v.segments.clear();
            } else {
                if (!isValidNumber(parsed)) return err("variable-replace: not a valid number");
                v.num = std::stod(parsed);
                v.segments.clear();
            }
        }
        return "";
    });

    defineCommand("variable-exist", [](const std::string& rest, const Ctx&) -> std::string {
        std::string rem = trim(rest);
        std::string varName;
        std::smatch m;
        static const std::regex dynRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*)RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
        if (std::regex_search(rem, m, dynRe)) {
            auto rv = readVarValue(m[1], "variable-exist");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("variable-exist: variable must be text");
            varName = rv.text;
            rem = trim(rem.substr(m.length()));
        } else if (std::regex_search(rem, m, litRe)) {
            varName = m[1];
            rem = trim(rem.substr(m.length()));
        } else return err("variable-exist: expected [\"name\"] or =[\"var\"]");
        static const std::regex trueRe(R"RX(^-\s*true\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, trueRe)) return err("variable-exist: missing \"-true=\" branch");
        rem = rem.substr(m.length());
        auto tr = parseBranch(rem, "variable-exist", "-true=");
        if (!tr.ok) return err(tr.error);
        rem = trim(rem.substr(tr.endPos));
        static const std::regex falseRe(R"RX(^-\s*false\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, falseRe)) return err("variable-exist: missing \"-false=\" branch");
        rem = rem.substr(m.length());
        auto fr = parseBranch(rem, "variable-exist", "-false=");
        if (!fr.ok) return err(fr.error);
        rem = trim(rem.substr(fr.endPos));
        if (!rem.empty()) return err("variable-exist: unexpected trailing text");
        if (tr.isNull && fr.isNull) return err("variable-exist: both branches cannot be (null)");
        bool exists = variables.count(varName);
        auto& b = exists ? tr : fr;
        if (b.isNull) return "";
        return processCommand(b.cmd, true);
    });

    // length"text" | length["var"] | length[list]-["name"] | length[functional]-["name"]
    // length[list]-["name"]"item" measures the length of the stored command string.
    defineCommand("length", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex listLenRe(R"RX(^\[\s*list\s*\]\s*-\s*\[\s*"([^"]+)"\s*\]\s*(?:"([^"]*)")?\s*$)RX", std::regex::icase);
        if (std::regex_search(rest, m, listLenRe)) {
            std::string ln = m[1];
            if (!lists.count(ln)) return err("length: list \"" + ln + "\" not found");
            if (!m[2].matched) return std::to_string(lists[ln].size());
            std::string item = m[2];
            if (!lists[ln].count(item)) return err("length: item \"" + item + "\" not found");
            return std::to_string(lists[ln][item].size());
        }
        static const std::regex funcLenRe(R"RX(^\[\s*functional\s*\]\s*-\s*\[\s*"([^"]+)"\s*\]\s*(?:-\s*(\d+))?\s*$)RX", std::regex::icase);
        if (std::regex_search(rest, m, funcLenRe)) {
            std::string fn = m[1];
            if (!functions.count(fn)) return err("length: function \"" + fn + "\" not found");
            const auto& cmds = functions[fn];
            if (!m[2].matched) return std::to_string(cmds.size());
            int idx = std::stoi(m[2]);
            if (idx < 1 || idx > (int)cmds.size()) return err("length: index out of range");
            return std::to_string(cmds[idx - 1].size());
        }
        if (!rest.empty() && rest[0] == '"') {
            static const std::regex re(R"RX(^"([^"]*)"\s*$)RX");
            if (!std::regex_search(rest, m, re)) return err("length: unterminated string");
            return std::to_string(m[1].str().size());
        }
        if (!rest.empty() && rest[0] == '[') {
            static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*$)RX");
            if (!std::regex_search(rest, m, re)) return err("length: invalid variable ref");
            std::string ne = varNameError(m[1]);
            if (!ne.empty()) return err("length: " + ne);
            if (!variables.count(m[1])) return err("length: variable not found");
            if (!variables[m[1]].isText) return err("length: only works on text");
            return std::to_string(variables[m[1]].str.size());
        }
        return err("length: bad syntax");
    });

    // input["var"]-text|number"prompt" reads one line from stdin.
    defineCommand("input", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]([\s\S]*)$)RX");
        if (!std::regex_search(rest, m, varRe)) return err("input: Usage: input[\"var\"]-text\"prompt\"");
        std::string varName = m[1];
        std::string rem = trim(m[2]);
        std::string ne = varNameError(varName);
        if (!ne.empty()) return err("input: " + ne);
        if (variables.count(varName)) return err("input: variable \"" + varName + "\" already exists");
        std::string varColor;
        static const std::regex colorRe(R"RX(^-color\s*\[\s*"([^"]+)"\s*\]\s*)RX", std::regex::icase);
        if (std::regex_search(rem, m, colorRe)) {
            varColor = m[1];
            std::string ce = colorError(varColor);
            if (!ce.empty()) return err("input: " + ce);
            rem = trim(rem.substr(m.length()));
        }
        static const std::regex typeRe(R"RX(^-?\s*(text|number)\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, typeRe)) return err("input: expected \"-text\" or \"-number\"");
        std::string type = toLower(m[1]);
        rem = rem.substr(m.length());
        std::vector<Segment> promptSegs;
        if (!trim(rem).empty()) {
            std::string pe;
            if (!parseMultiColorValue(rem, varColor, true, promptSegs, pe)) return err("input: " + pe);
        }
        if (!promptSegs.empty()) rawOut(renderSegments(promptSegs));
        std::string line;
        if (!std::getline(std::cin, line)) line = "";
        line = sanitizeAscii(line);
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        std::replace(line.begin(), line.end(), '\n', ' ');
        Variable v;
        if (type == "text") {
            v.isText = true;
            v.str = line;
        } else {
            if (!isValidNumber(line)) {
                rawOut(err("input: \"" + line + "\" is not a valid number; set to 0") + "\n");
                v.isText = false;
                v.num = 0;
            } else {
                v.isText = false;
                v.num = std::stod(line);
            }
        }
        if (!varColor.empty()) v.color = varColor;
        variables[varName] = v;
        return "";
    });

    // input-choose has three variants:
    //   1. input-choose["var"]-text"prompt"-"opt"="val"...   (store value into var)
    //   2. input-choose"prompt"-"opt"=(cmd)...               (execute command on choice)
    //   3. input-choose[long-press]...                       (hold key for N seconds)
    // The long-press variant has both store (`="value"`) and exec (`=(cmd)`) forms.
    defineCommand("input-choose", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex lpRe(R"RX(^\[\s*long-press\s*\]([\s\S]*)$)RX", std::regex::icase);
        if (std::regex_search(rest, m, lpRe)) {
            std::string inner = m[1];
            static const std::regex lpVarLead(R"RX(^-\s*\[\s*")RX");
            if (std::regex_search(inner, lpVarLead)) {
                static const std::regex vnRe(R"RX(^-\s*\[\s*"([^"]+)"\s*\]\s*)RX");
                if (!std::regex_search(inner, m, vnRe)) return err("input-choose[long-press]: bad syntax");
                std::string varName = m[1];
                std::string rem = trim(inner.substr(m.length()));
                std::string ne = varNameError(varName);
                if (!ne.empty()) return err("input-choose[long-press]: " + ne);
                if (variables.count(varName)) return err("input-choose[long-press]: variable \"" + varName + "\" already exists");
                std::string varColor;
                static const std::regex colorRe(R"RX(^-color\s*\[\s*"([^"]+)"\s*\]\s*)RX", std::regex::icase);
                if (std::regex_search(rem, m, colorRe)) {
                    varColor = m[1];
                    std::string ce = colorError(varColor);
                    if (!ce.empty()) return err("input-choose[long-press]: " + ce);
                    rem = trim(rem.substr(m.length()));
                }
                static const std::regex typeRe(R"RX(^-?\s*(text|number)\s*)RX", std::regex::icase);
                if (!std::regex_search(rem, m, typeRe)) return err("input-choose[long-press]: missing type");
                std::string type = toLower(m[1]);
                rem = rem.substr(m.length());
                size_t pos = std::string::npos;
                {
                    static const std::regex tRe(R"RX(-\s*time\s*\[)RX", std::regex::icase);
                    std::smatch tm;
                    if (std::regex_search(rem, tm, tRe)) pos = tm.position(0);
                }
                if (pos == std::string::npos) return err("input-choose[long-press]: expected at least one -time entry");
                std::string promptExpr = trim(rem.substr(0, pos));
                rem = rem.substr(pos);
                std::vector<Segment> promptSegs;
                if (!promptExpr.empty()) {
                    std::string pe;
                    if (!parseMultiColorValue(promptExpr, varColor, true, promptSegs, pe)) return err("input-choose[long-press]: " + pe);
                }
                struct LPEntry {
                    std::string key;
                    int time;
                    std::string value;
                };
                std::vector<LPEntry> entries;
                static const std::regex timeRe(R"RX(^-\s*time\s*\[\s*"(\d+)"\s*\]\s*"([^"]*)"\s*=\s*"([^"]*)"\s*)RX", std::regex::icase);
                while (!trim(rem).empty()) {
                    rem = ltrim(rem);
                    std::smatch tm;
                    if (!std::regex_search(rem, tm, timeRe)) return err("input-choose[long-press]: expected -time[\"N\"]\"key\"=\"value\"");
                    int ts = std::stoi(tm[1]);
                    std::string key = tm[2], value = tm[3];
                    if (key.size() != 1) return err("input-choose[long-press]: key must be a single character");
                    if (ts <= 0) return err("input-choose[long-press]: time must be positive");
                    entries.push_back({ key, ts, value });
                    rem = rem.substr(tm.length());
                }
                if (entries.empty()) return err("input-choose[long-press]: no time entries");
                if (type == "number") for (auto& e : entries)
                    if (!isValidNumber(e.value)) return err("input-choose[long-press]: value \"" + e.value + "\" is not a valid number");
                if (!promptSegs.empty()) rawOut(renderSegments(promptSegs));
                rawOut("\r\ninput-choose>[] (press any key listed above)");
                int c = readKeyWithTimeout(-1);
                if (c <= 0) return "";
                std::string key(1, (char)c);
                std::vector<LPEntry*> matching;
                for (auto& e : entries) if (e.key == key) matching.push_back(&e);
                if (matching.empty()) {
                    rawOut("\r\033[Kinput-choose>[" + key + "]\r\n");
                    rawOut(hint("That option does not exist.") + "\r\n");
                    return "";
                }
                std::sort(matching.begin(), matching.end(),
                          [](LPEntry* a, LPEntry* b) { return a->time < b->time; });
                int heldSec = measureHoldDuration(key, matching.back()->time, "input-choose>");
                if (heldSec < 0) {
                    rawOut("\r\n" + hint("Cancelled.") + "\r\n");
                    return "";
                }
                LPEntry* chosen = nullptr;
                for (auto* e : matching)
                    if (e->time <= heldSec && (!chosen || e->time > chosen->time)) chosen = e;
                if (!chosen) {
                    rawOut("\r\n" + hint("Cancelled (released too early).") + "\r\n");
                    return "";
                }
                Variable v;
                if (type == "text") {
                    v.isText = true;
                    v.str = chosen->value;
                } else {
                    v.isText = false;
                    v.num = std::stod(chosen->value);
                }
                if (!varColor.empty()) v.color = varColor;
                variables[varName] = v;
                rawOut("\r\n" + hint("Triggered: key '" + key + "' -> variable \"" + varName + "\"") + "\r\n");
                return "";
            } else {
                auto parseRes = parseLongPressExec(inner);
                if (!parseRes.ok) return err("input-choose[long-press]: " + parseRes.error);
                if (!parseRes.promptSegs.empty()) rawOut(renderSegments(parseRes.promptSegs));
                rawOut("\r\ninput-choose>[] (press any key listed above)");
                int c = readKeyWithTimeout(-1);
                if (c <= 0) return "";
                std::string key(1, (char)c);
                std::vector<const LongPressExecResult::Entry*> matching;
                for (auto& e : parseRes.entries) if (e.key == key) matching.push_back(&e);
                if (matching.empty()) {
                    rawOut("\r\033[Kinput-choose>[" + key + "]\r\n");
                    rawOut(hint("That option does not exist.") + "\r\n");
                    return "";
                }
                std::sort(matching.begin(), matching.end(),
                          [](const LongPressExecResult::Entry* a, const LongPressExecResult::Entry* b) {
                              return a->time < b->time;
                          });
                int heldSec = measureHoldDuration(key, matching.back()->time, "input-choose>");
                if (heldSec < 0) {
                    rawOut("\r\n" + hint("Cancelled.") + "\r\n");
                    return "";
                }
                const LongPressExecResult::Entry* chosen = nullptr;
                for (auto* e : matching)
                    if (e->time <= heldSec && (!chosen || e->time > chosen->time)) chosen = e;
                if (!chosen) {
                    rawOut("\r\n" + hint("Cancelled (released too early).") + "\r\n");
                    return "";
                }
                rawOut("\r\n" + hint("Triggered: key '" + key + "'") + "\r\n");
                return processCommand(chosen->cmd, true);
            }
        }
        if (!rest.empty() && rest[0] != '[') {
            // exec form: "prompt" "opt"=(cmd)...
            std::vector<Segment> promptSegs;
            struct Opt {
                std::string name, cmd;
            };
            std::vector<Opt> options;
            size_t i = 0;
            bool scanning = true;
            while (i < rest.size()) {
                while (i < rest.size() && std::isspace((unsigned char)rest[i])) i++;
                if (i >= rest.size()) break;
                if (rest[i] == '"') {
                    size_t j = i + 1;
                    while (j < rest.size() && rest[j] != '"') j++;
                    if (j >= rest.size()) return err("input-choose: unterminated string");
                    std::string content = rest.substr(i + 1, j - i - 1);
                    size_t k = j + 1;
                    while (k < rest.size() && std::isspace((unsigned char)rest[k])) k++;
                    if (k < rest.size() && rest[k] == '=' && scanning) {
                        k++;
                        while (k < rest.size() && std::isspace((unsigned char)rest[k])) k++;
                        if (k >= rest.size() || rest[k] != '(') return err("input-choose: expected \"(\" after \"=\"");
                        auto p = extractParenthesized(rest, k);
                        if (!p.found) return err("input-choose: missing \")\"");
                        options.push_back({ content, p.content });
                        i = p.endPos;
                        scanning = false;
                        continue;
                    }
                    if (scanning) promptSegs.push_back({ "", content });
                    i = j + 1;
                    continue;
                }
                if (rest[i] == '-') {
                    i++;
                    while (i < rest.size() && std::isspace((unsigned char)rest[i])) i++;
                    std::string sub = rest.substr(i);
                    std::smatch cm;
                    static const std::regex colorRe(R"RX(^color\s*\[\s*"([^"]+)"\s*\])RX", std::regex::icase);
                    if (std::regex_search(sub, cm, colorRe)) { i += cm.length(); continue; }
                    if (startsWithCI(sub, "nr-")) { i += 3; continue; }
                    if (startsWithCI(sub, "n-")) { i += 2; continue; }
                    if (startsWithCI(sub, "r-")) { i += 2; continue; }
                    continue;
                }
                return err("input-choose: unexpected character");
            }
            if (options.empty()) return err("input-choose: no options specified");
            if (!promptSegs.empty()) rawOut(renderSegments(promptSegs));
            rawOut("input-choose> ");
            std::string line;
            if (!std::getline(std::cin, line)) line = "";
            line = trim(sanitizeAscii(line));
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            for (auto& o : options) if (o.name == line) return processCommand(o.cmd, true);
            return hint("That option does not exist. Please enter a valid option.");
        }
        // store form: input-choose["var"]-text"prompt"-"opt"="val"...
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]([\s\S]*)$)RX");
        if (!std::regex_search(rest, m, varRe)) return err("input-choose: bad syntax");
        std::string varName = m[1];
        std::string rem = trim(m[2]);
        std::string ne = varNameError(varName);
        if (!ne.empty()) return err("input-choose: " + ne);
        if (variables.count(varName)) return err("input-choose: variable \"" + varName + "\" already exists");
        std::string varColor;
        static const std::regex colorRe(R"RX(^-color\s*\[\s*"([^"]+)"\s*\]\s*)RX", std::regex::icase);
        if (std::regex_search(rem, m, colorRe)) {
            varColor = m[1];
            std::string ce = colorError(varColor);
            if (!ce.empty()) return err("input-choose: " + ce);
            rem = trim(rem.substr(m.length()));
        }
        static const std::regex typeRe(R"RX(^-?\s*(text|number)\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, typeRe)) return err("input-choose: missing type");
        std::string type = toLower(m[1]);
        rem = rem.substr(m.length());
        size_t optStart = std::string::npos;
        {
            static const std::regex optMarker(R"RX(-"[^"]+"\s*=\s*")RX");
            std::smatch om;
            if (std::regex_search(rem, om, optMarker)) optStart = om.position(0);
        }
        std::string promptExpr = (optStart != std::string::npos) ? trim(rem.substr(0, optStart)) : trim(rem);
        std::string optsStr = (optStart != std::string::npos) ? rem.substr(optStart) : "";
        std::vector<Segment> promptSegs;
        if (!promptExpr.empty()) {
            std::string pe;
            if (!parseMultiColorValue(promptExpr, varColor, true, promptSegs, pe)) return err("input-choose: " + pe);
        }
        std::map<std::string, std::string> options;
        size_t oi = 0;
        while (oi < optsStr.size()) {
            while (oi < optsStr.size() && std::isspace((unsigned char)optsStr[oi])) oi++;
            if (oi >= optsStr.size()) break;
            if (optsStr[oi] != '-') return err("input-choose: expected \"-\" before option");
            oi++;
            while (oi < optsStr.size() && std::isspace((unsigned char)optsStr[oi])) oi++;
            if (oi >= optsStr.size() || optsStr[oi] != '"') return err("input-choose: expected option name in quotes");
            size_t j = oi + 1;
            while (j < optsStr.size() && optsStr[j] != '"') j++;
            if (j >= optsStr.size()) return err("input-choose: unterminated option name");
            std::string optName = optsStr.substr(oi + 1, j - oi - 1);
            oi = j + 1;
            while (oi < optsStr.size() && std::isspace((unsigned char)optsStr[oi])) oi++;
            if (oi >= optsStr.size() || optsStr[oi] != '=') return err("input-choose: expected \"=\"");
            oi++;
            while (oi < optsStr.size() && std::isspace((unsigned char)optsStr[oi])) oi++;
            if (oi >= optsStr.size() || optsStr[oi] != '"') return err("input-choose: expected value in quotes");
            j = oi + 1;
            while (j < optsStr.size() && optsStr[j] != '"') j++;
            if (j >= optsStr.size()) return err("input-choose: unterminated option value");
            std::string optValue = optsStr.substr(oi + 1, j - oi - 1);
            oi = j + 1;
            if (options.count(optName)) return err("input-choose: duplicate option");
            options[optName] = optValue;
        }
        if (options.empty()) return err("input-choose: no options specified");
        if (type == "number") for (auto& kv : options)
            if (!isValidNumber(kv.second)) return err("input-choose: option \"" + kv.first + "\" value not a valid number");
        if (!promptSegs.empty()) rawOut(renderSegments(promptSegs));
        rawOut("input-choose> ");
        std::string line;
        if (!std::getline(std::cin, line)) line = "";
        line = trim(sanitizeAscii(line));
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        if (!options.count(line)) return hint("That option does not exist. Please enter a valid option.");
        Variable v;
        if (type == "text") {
            v.isText = true;
            v.str = options[line];
        } else {
            v.isText = false;
            v.num = std::stod(options[line]);
        }
        if (!varColor.empty()) v.color = varColor;
        variables[varName] = v;
        return "";
    }, { {"branch", R"RX(^-\s*time\s*\[\s*"[^"]*"\s*\]\s*"[^"]*"\s*=\s*\()RX"}, {"branch", R"RX(^-\s*"[^"]*"\s*=\s*\()RX"} });

    // save"name" saves plain; save"name"-xor saves XOR-encrypted.
    defineCommand("save", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^"([^"]+)"(?:\s*-\s*(xor))?\s*$)RX", std::regex::icase);
        if (!std::regex_search(rest, m, re)) return err("save: Usage: save\"name\" or save\"name\"-xor");
        std::string name = m[1];
        if (name.empty()) return err("save: name cannot be empty");
        bool enc = m[2].matched;
        std::string json = saveToFileData();
        std::string path = name + FILE_EXT;
        try {
            std::ofstream f(path, std::ios::binary);
            if (!f) return err("save: cannot open file \"" + path + "\"");
            if (enc) {
                std::vector<unsigned char> h(CS_MAGIC_XOR.begin(), CS_MAGIC_XOR.end());
                std::vector<unsigned char> p(json.begin(), json.end());
                auto x = xorBytes(p);
                f.write((const char*)h.data(), h.size());
                f.write((const char*)x.data(), x.size());
            } else {
                f.write(CS_MAGIC_TEXT.data(), CS_MAGIC_TEXT.size());
                f.write(json.data(), json.size());
            }
        } catch (...) { return err("save: write failed"); }
        return hint("Saved as " + path + (enc ? " (XOR-encrypted)" : ""));
    });

    // load prompts for a path, reads the file, detects plain vs XOR by magic.
    defineCommand("load", [](const std::string& rest, const Ctx&) -> std::string {
        if (!trim(rest).empty()) return err("load: takes no arguments");
        rawOut("Load file (enter path): ");
        std::string path;
        if (!std::getline(std::cin, path)) return err("load: no input");
        path = trim(sanitizeAscii(path));
        path.erase(std::remove(path.begin(), path.end(), '\r'), path.end());
        if (path.size() < FILE_EXT.size() || path.substr(path.size() - FILE_EXT.size()) != FILE_EXT) return err("load: unsupported file extension");
        std::ifstream f(path, std::ios::binary);
        if (!f) return err("load: cannot open file");
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.empty()) return err("load: file is empty");
        std::string jsonText;
        bool isXor = false;
        std::vector<unsigned char> xh(CS_MAGIC_XOR.begin(), CS_MAGIC_XOR.end()), ph(CS_MAGIC_TEXT.begin(), CS_MAGIC_TEXT.end());
        if (bytes.size() >= xh.size() && std::equal(xh.begin(), xh.end(), bytes.begin())) isXor = true;
        if (isXor) {
            std::vector<unsigned char> p(bytes.begin() + xh.size(), bytes.end());
            auto d = xorBytes(p);
            jsonText.assign(d.begin(), d.end());
        } else {
            if (bytes.size() < ph.size()) return err("load: file too short");
            if (!std::equal(ph.begin(), ph.end(), bytes.begin())) return err("load: bad magic");
            jsonText.assign(bytes.begin() + ph.size(), bytes.end());
        }
        std::string em;
        if (!applyLoadedData(jsonText, em)) return err("load: " + em);
        return hint("Loaded successfully from " + path + (isXor ? " (XOR-encrypted)" : ""));
    });

    defineCommand("text", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex concatRe(R"RX(^\[\s*concatenation\s*\]\s*-?\s*"([\s\S]*)"\s*$)RX", std::regex::icase);
        if (std::regex_search(rest, m, concatRe)) {
            std::string result, e;
            if (!evalExpression(m[1], result, e)) return err("text[concatenation]: " + e);
            return result;
        }
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]([\s\S]*)$)RX");
        if (std::regex_search(rest, m, varRe)) {
            std::string varName = m[1];
            std::string after = trim(m[2]);
            std::string ne = varNameError(varName);
            if (!ne.empty()) return err("text: " + ne);
            if (after.empty()) {
                if (!variables.count(varName)) return err("text: variable \"" + varName + "\" not found");
                if (!variables[varName].isText) return err("text: variable is not text type");
                return renderVariable(variables[varName]);
            }
            if (variables.count(varName)) return err("text: variable already exists.");
            std::vector<Segment> segs;
            std::string pe;
            if (!parseMultiColorValue(after, "", true, segs, pe)) return err("text: " + pe);
            Variable v;
            std::string be;
            if (!buildVariable(false, segs, true, v, be)) return err("text: " + be);
            variables[varName] = v;
            return "";
        }
        if (!rest.empty() && rest[0] == '"') {
            std::vector<Segment> segs;
            std::string pe;
            if (!parseMultiColorValue(rest, "", true, segs, pe)) return err("text: " + pe);
            return renderSegments(segs);
        }
        return err("text: bad syntax");
    }, { {"flag", R"RX(^-\s*color\s*\[\s*"[^"]*"\s*\])RX"}, {"flag", R"RX(^-\s*nr-)RX"}, {"flag", R"RX(^-\s*n-)RX"}, {"flag", R"RX(^-\s*r-)RX"} });

    defineCommand("number", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex concatRe(R"RX(^\[\s*concatenation\s*\]\s*-?\s*"([\s\S]*)"\s*$)RX", std::regex::icase);
        if (std::regex_search(rest, m, concatRe)) {
            std::string result, e;
            if (!evalConcat(m[1], result, e)) return err("number[concatenation]: " + e);
            if (!isValidNumber(result)) return err("number[concatenation]: not a valid number");
            return formatNumber(std::stod(result));
        }
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]([\s\S]*)$)RX");
        if (std::regex_search(rest, m, varRe)) {
            std::string varName = m[1];
            std::string after = trim(m[2]);
            std::string ne = varNameError(varName);
            if (!ne.empty()) return err("number: " + ne);
            if (after.empty()) {
                if (!variables.count(varName)) return err("number: variable \"" + varName + "\" not found");
                if (variables[varName].isText) return err("number: variable is not number type");
                return renderVariable(variables[varName]);
            }
            if (variables.count(varName)) return err("number: variable already exists.");
            std::vector<Segment> segs;
            std::string pe;
            if (!parseMultiColorValue(after, "", false, segs, pe)) return err("number: " + pe);
            Variable v;
            std::string be;
            if (!buildVariable(true, segs, false, v, be)) return err("number: " + be);
            variables[varName] = v;
            return "";
        }
        if (!rest.empty() && rest[0] == '"') {
            std::vector<Segment> segs;
            std::string pe;
            if (!parseMultiColorValue(rest, "", false, segs, pe)) return err("number: " + pe);
            std::string full;
            for (auto& s : segs) full += s.text;
            if (!isValidNumber(full)) return err("number: \"" + full + "\" is not a valid number");
            return renderSegments(segs);
        }
        return err("number: bad syntax");
    }, { {"flag", R"RX(^-\s*color\s*\[\s*"[^"]*"\s*\])RX"}, {"flag", R"RX(^-\s*nr-)RX"}, {"flag", R"RX(^-\s*n-)RX"}, {"flag", R"RX(^-\s*r-)RX"} });

    defineCommand("text-color", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*([\s\S]*)$)RX");
        if (!std::regex_search(rest, m, re)) return err("text-color: Usage: text-color[\"color\"]\"text\"");
        std::string ce = colorError(m[1]);
        if (!ce.empty()) return err("text-color: " + ce);
        std::vector<Segment> segs;
        std::string pe;
        if (!parseMultiColorValue(m[2], m[1], true, segs, pe)) return err("text-color: " + pe);
        return renderSegments(segs);
    }, { {"flag", R"RX(^-\s*color\s*\[\s*"[^"]*"\s*\])RX"}, {"flag", R"RX(^-\s*nr-)RX"}, {"flag", R"RX(^-\s*n-)RX"}, {"flag", R"RX(^-\s*r-)RX"} });

    defineCommand("number-color", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex re(R"RX(^\[\s*"([^"]+)"\s*\]\s*([\s\S]*)$)RX");
        if (!std::regex_search(rest, m, re)) return err("number-color: Usage: number-color[\"color\"]\"123\"");
        std::string ce = colorError(m[1]);
        if (!ce.empty()) return err("number-color: " + ce);
        std::vector<Segment> segs;
        std::string pe;
        if (!parseMultiColorValue(m[2], m[1], false, segs, pe)) return err("number-color: " + pe);
        return renderSegments(segs);
    }, { {"flag", R"RX(^-\s*color\s*\[\s*"[^"]*"\s*\])RX"}, {"flag", R"RX(^-\s*nr-)RX"}, {"flag", R"RX(^-\s*n-)RX"}, {"flag", R"RX(^-\s*r-)RX"} });

    // computation supports modes: default, [modulo], [round], [variable], [variable-modulo], [variable-round].
    // `variable*` modes store the result into -["target"].
    defineCommand("computation", [](const std::string& rest, const Ctx&) -> std::string {
        std::string rem = trim(rest);
        std::string mode;
        std::smatch m;
        static const std::regex modeRe(R"RX(^\[\s*([^\]]+)\s*\]\s*)RX", std::regex::icase);
        if (std::regex_search(rem, m, modeRe)) {
            std::string mv = toLower(trim(m[1].str()));
            if (mv == "modulo" || mv == "round" || mv == "variable" || mv == "variable-modulo" || mv == "variable-round") {
                mode = mv;
                rem = rem.substr(m.length());
            }
        }
        if (!mode.empty()) {
            rem = ltrim(rem);
            if (!rem.empty() && rem[0] == '-') rem = trim(rem.substr(1));
        }
        auto exprR = parseComputationExpr(rem);
        if (!exprR.ok) return err("computation: " + exprR.error);
        std::string expr = exprR.expr;
        std::string after = trim(exprR.remaining);
        std::string varName;
        if (!after.empty() && after[0] == '-') {
            static const std::regex varRe(R"RX(^-\s*\[\s*"([^"]+)"\s*\]\s*)RX");
            if (std::regex_search(after, m, varRe)) {
                varName = m[1];
                after = trim(after.substr(m.length()));
            }
        }
        std::string colorName;
        if (!after.empty() && after[0] == '-') {
            static const std::regex colorRe(R"RX(^-\s*color\s*\[\s*"([^"]+)"\s*\]\s*$)RX", std::regex::icase);
            if (std::regex_search(after, m, colorRe)) {
                colorName = m[1];
                std::string ce = colorError(colorName);
                if (!ce.empty()) return err("computation: " + ce);
                after = "";
            }
        }
        if (!after.empty()) return err("computation: unexpected trailing text \"" + after + "\"");
        if (!varName.empty()) {
            if (mode.empty()) mode = "variable";
            else if (mode == "round") mode = "variable-round";
            else if (mode == "modulo") mode = "variable-modulo";
        }
        if (mode.empty()) {
            std::string result, e;
            if (!evalExpression(expr, result, e)) return err("computation: " + e);
            return colorName.empty() ? result : colorAnsi(colorName) + result + "\033[0m";
        }
        if (mode == "modulo") {
            std::string result, e;
            if (!evalModulo(expr, result, e)) return err("computation: " + e);
            return colorName.empty() ? result : colorAnsi(colorName) + result + "\033[0m";
        }
        if (mode == "round") {
            std::string result, e;
            if (!evalRound(expr, result, e)) return err("computation: " + e);
            return colorName.empty() ? result : colorAnsi(colorName) + result + "\033[0m";
        }
        if (varName.empty()) return err("computation: mode \"" + mode + "\" requires a target variable");
        std::string ne = varNameError(varName);
        if (!ne.empty()) return err("computation: " + ne);
        std::string result, e;
        if (mode == "variable") {
            if (!evalExpression(expr, result, e)) return err("computation: " + e);
        } else if (mode == "variable-modulo") {
            if (!evalModulo(expr, result, e)) return err("computation: " + e);
        } else {
            if (!evalRound(expr, result, e)) return err("computation: " + e);
        }
        Variable v;
        if (mode == "variable" && !isValidNumber(result)) {
            v.isText = true;
            v.str = result;
        } else {
            v.isText = false;
            v.num = std::stod(result);
        }
        if (!colorName.empty()) v.color = colorName;
        variables[varName] = v;
        return "";
    });

    // time"N"-command or time["var"]-command: schedules in the background.
    defineCommand("time", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex litRe(R"RX(^"(\d+)"\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        int delay = 0;
        std::string inner;
        if (std::regex_search(rest, m, litRe)) {
            delay = std::stoi(m[1]);
            inner = trim(m[2]);
        } else if (std::regex_search(rest, m, varRe)) {
            std::string vn = m[1];
            std::string ne = varNameError(vn);
            if (!ne.empty()) return err("time: " + ne);
            if (!variables.count(vn)) return err("time: variable \"" + vn + "\" not found");
            if (variables[vn].isText) return err("time: variable is not number");
            double d = variables[vn].num;
            if (d < 0 || d != std::floor(d)) return err("time: delay must be non-negative integer");
            delay = (int)d;
            inner = trim(m[2]);
        } else return err("time: Usage: time\"seconds\"-command");
        if (inner.empty()) return err("time: missing command");
        scheduleDelayedCommand(delay, inner);
        return "Scheduled in " + std::to_string(delay) + "s: " + inner;
    });

    defineCommand("run", [](const std::string& rest, const Ctx&) -> std::string {
        auto subs = extractSubCommands(rest);
        if (subs.empty()) return err("run: bad syntax");
        executeSequentialCommands(subs);
        return "";
    });

    // while<cond>-do(cmd1)-(cmd2)-...
    defineCommand("while", [](const std::string& rest, const Ctx&) -> std::string {
        auto condR = buildCondition(rest, "while");
        if (!condR.ok) return err(condR.error);
        std::string rem = condR.remaining;
        std::smatch m;
        static const std::regex doRe(R"RX(^-?\s*do\s*=?\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, doRe)) return err("while: expected -do(...) or -do=(...)");
        rem = rem.substr(m.length());
        auto subs = extractSubCommands(rem);
        if (subs.empty()) return err("while: -do(...) must contain at least one command");
        while (condR.check()) {
            for (auto& s : subs) {
                std::string out = processCommand(s, true);
                if (!out.empty()) rawOut(out + "\n");
            }
        }
        return "";
    }, { {"branch", R"RX(^-\s*do\s*=?\s*\()RX"} });

    // random family:
    //   random[shuffle]-...            shuffle values / commands
    //   random["a"]-["b"]...            number range or pick-one-value
    //   random"N"-text/-number/(cmds)   pick N
    //   random -uuid                    generate a UUID
    defineCommand("random", [](const std::string& rest, const Ctx&) -> std::string {
        std::string t = trim(rest);
        static std::mt19937 rng{ std::random_device{}() };
        std::smatch m;
        static const std::regex shuffleRe(R"RX(^\[\s*shuffle\s*\]([\s\S]*)$)RX", std::regex::icase);
        if (std::regex_search(t, m, shuffleRe)) {
            std::string inner = trim(m[1]);
            if (!inner.empty() && (inner[0] == '-' || inner[0] == '[')) {
                std::string r = inner;
                if (!r.empty() && r[0] == '-') r = trim(r.substr(1));
                if (!r.empty() && r[0] == '[') {
                    std::vector<std::string> varNames;
                    std::string vErr;
                    while (!r.empty()) {
                        static const std::regex vRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
                        std::smatch vm;
                        if (!std::regex_search(r, vm, vRe)) { vErr = "expected [\"varName\"]"; break; }
                        varNames.push_back(vm[1]);
                        r = trim(r.substr(vm.length()));
                        if (!r.empty() && r[0] == '-') r = trim(r.substr(1));
                        else if (!r.empty()) { vErr = "unexpected text after variable reference"; break; }
                    }
                    if (!vErr.empty()) return err("random[shuffle]: " + vErr);
                    if (varNames.empty()) return err("random[shuffle]: no variables specified");
                    std::vector<std::string> vals;
                    bool typeSet = false, isText = false;
                    for (auto& vn : varNames) {
                        auto rv = readVarValue(vn, "random[shuffle]");
                        if (!rv.ok) return err(rv.error);
                        if (!typeSet) { isText = rv.isText; typeSet = true; }
                        else if (isText != rv.isText) return err("random[shuffle]: cannot mix number and text variables");
                        vals.push_back(rv.text);
                    }
                    std::shuffle(vals.begin(), vals.end(), rng);
                    std::string out;
                    for (auto& v : vals) out += v;
                    return out;
                }
            }
            static const std::regex typeRe(R"RX(^-\s*(text|number)\s*([\s\S]*)$)RX", std::regex::icase);
            if (std::regex_search(inner, m, typeRe)) {
                std::string type = toLower(m[1]);
                std::vector<std::string> args;
                static const std::regex q(R"RX("([^"]*)")RX");
                for (auto it = std::sregex_iterator(m[2].str().begin(), m[2].str().end(), q); it != std::sregex_iterator(); ++it) args.push_back((*it)[1]);
                if (args.empty()) return err("random[shuffle]-" + type + ": need at least one quoted arg");
                if (type == "number") for (auto& a : args)
                    if (!isValidNumber(a)) return err("random[shuffle]-number: \"" + a + "\" is not a valid number");
                std::shuffle(args.begin(), args.end(), rng);
                std::string out;
                for (auto& a : args) out += a;
                return out;
            }
            if (!inner.empty() && inner[0] == '(') {
                auto subs = extractSubCommands(inner);
                if (subs.empty()) return err("random[shuffle]: bad command list");
                std::shuffle(subs.begin(), subs.end(), rng);
                std::string out;
                for (auto& s : subs) {
                    std::string r2 = processCommand(s, true);
                    if (!r2.empty()) out += r2 + "\n";
                }
                if (!out.empty() && out.back() == '\n') out.pop_back();
                return out;
            }
            return err("random[shuffle]: bad syntax");
        }
        static const std::regex varFirstRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX");
        if (std::regex_search(t, m, varFirstRe)) {
            std::vector<std::string> varNames = { m[1] };
            std::string r = trim(m[2]);
            static const std::regex vRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
            while (!r.empty()) {
                std::smatch mm;
                if (!std::regex_search(r, mm, vRe)) return err("random: expected [\"varName\"]");
                varNames.push_back(mm[1]);
                r = trim(r.substr(mm.length()));
                if (!r.empty() && r[0] == '-') r = trim(r.substr(1));
                else if (!r.empty()) return err("random: unexpected text");
            }
            if (varNames.size() < 2) return err("random: need at least 2 variables");
            std::vector<VarRead> vals;
            bool typeSet = false, isText = false;
            for (auto& vn : varNames) {
                auto rv = readVarValue(vn, "random");
                if (!rv.ok) return err(rv.error);
                if (!typeSet) { isText = rv.isText; typeSet = true; }
                else if (isText != rv.isText) return err("random: cannot mix number and text variables");
                vals.push_back(rv);
            }
            if (!isText && vals.size() == 2) {
                long long a = (long long)vals[0].num, b = (long long)vals[1].num;
                long long lo = std::min(a, b), hi = std::max(a, b);
                std::uniform_int_distribution<long long> d(lo, hi);
                return std::to_string(d(rng));
            }
            std::uniform_int_distribution<size_t> d2(0, vals.size() - 1);
            return vals[d2(rng)].text;
        }
        static const std::regex ntextRe(R"RX(^"(\d+)"\s*-\s*text\s*([\s\S]*)$)RX", std::regex::icase);
        if (std::regex_search(t, m, ntextRe)) {
            int count = std::stoi(m[1]);
            std::vector<std::string> texts;
            static const std::regex q(R"RX("([^"]*)")RX");
            for (auto it = std::sregex_iterator(m[2].str().begin(), m[2].str().end(), q); it != std::sregex_iterator(); ++it) texts.push_back((*it)[1]);
            if (texts.empty()) return err("random: no texts");
            if (count <= 0 || count > (int)texts.size()) return err("random: count out of range");
            std::shuffle(texts.begin(), texts.end(), rng);
            std::string out;
            for (int i = 0; i < count; ++i) {
                if (i) out += "\n";
                out += texts[i];
            }
            return out;
        }
        static const std::regex nnumRe(R"RX(^"(\d+)"\s*-\s*number\s*"(-?\d+)"\s*-\s*"(-?\d+)"\s*$)RX", std::regex::icase);
        if (std::regex_search(t, m, nnumRe)) {
            int count = std::stoi(m[1]), mn = std::stoi(m[2]), mx = std::stoi(m[3]);
            if (mn > mx) return err("random: min > max");
            int rangeSize = mx - mn + 1;
            if (count <= 0 || count > rangeSize) return err("random: count out of range");
            std::vector<int> pool;
            for (int i = mn; i <= mx; ++i) pool.push_back(i);
            std::shuffle(pool.begin(), pool.end(), rng);
            std::string out;
            for (int i = 0; i < count; ++i) {
                if (i) out += "\n";
                out += std::to_string(pool[i]);
            }
            return out;
        }
        static const std::regex nmultiRe(R"RX(^"(\d+)"\s*-\s*(\([\s\S]*)$)RX");
        if (std::regex_search(t, m, nmultiRe)) {
            int count = std::stoi(m[1]);
            auto subs = extractSubCommands(trim(m[2]));
            if (subs.empty()) return err("random: bad command list");
            if (count <= 0 || count > (int)subs.size()) return err("random: count out of range");
            std::shuffle(subs.begin(), subs.end(), rng);
            std::vector<std::string> sel(subs.begin(), subs.begin() + count);
            executeSequentialCommands(sel);
            return "";
        }
        if (!t.empty() && t[0] == '(') {
            auto subs = extractSubCommands(t);
            if (subs.empty()) return err("random: bad command list");
            std::uniform_int_distribution<size_t> d(0, subs.size() - 1);
            return processCommand(subs[d(rng)], true);
        }
        if (toLower(t) == "-uuid") {
            std::uniform_int_distribution<int> d(0, 255);
            unsigned char bytes[16];
            for (int i = 0; i < 16; ++i) bytes[i] = (unsigned char)d(rng);
            bytes[6] = (bytes[6] & 0x0f) | 0x40;
            bytes[8] = (bytes[8] & 0x3f) | 0x80;
            char buf[37];
            std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
                bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
            return std::string(buf);
        }
        return err("random: bad syntax.");
    });

    defineCommand("repeat", [](const std::string& rest, const Ctx&) -> std::string {
        std::smatch m;
        static const std::regex litRe(R"RX(^"(\d+)"\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        int count = 0;
        std::string inner;
        if (std::regex_search(rest, m, litRe)) {
            count = std::stoi(m[1]);
            inner = trim(m[2]);
        } else if (std::regex_search(rest, m, varRe)) {
            std::string vn = m[1];
            std::string ne = varNameError(vn);
            if (!ne.empty()) return err("repeat: " + ne);
            if (!variables.count(vn)) return err("repeat: variable not found");
            if (variables[vn].isText) return err("repeat: variable not number");
            double d = variables[vn].num;
            if (d != std::floor(d)) return err("repeat: count must be integer");
            count = (int)d;
            inner = trim(m[2]);
        } else return err("repeat: bad syntax");
        if (count <= 0) return err("repeat: count must be positive integer");
        if (inner.empty()) return err("repeat: missing command");
        // If the inner command itself is a time"..."-cmd, run it N times on a schedule.
        static const std::regex timeRe(R"RX(^time\s*(?:"(\d+)"|\[\s*"([^"]+)"\s*\])\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        if (std::regex_search(inner, m, timeRe)) {
            int delay = 0;
            if (m[1].length()) delay = std::stoi(m[1]);
            else {
                if (!variables.count(m[2])) return err("repeat: time var not found");
                if (variables[m[2]].isText) return err("repeat: time var not number");
                delay = (int)variables[m[2]].num;
            }
            std::string actual = trim(m[3]);
            if (actual.empty()) return err("repeat: missing command");
            std::thread([count, delay, actual]() {
                for (int i = 0; i < count; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(delay));
                    std::string out = processCommand(actual, true);
                    if (!out.empty()) rawOut(out + "\n");
                }
            }).detach();
            return "Scheduled " + std::to_string(count) + " times every " + std::to_string(delay) + "s";
        }
        std::string combined;
        for (int i = 0; i < count; ++i) {
            std::string out = processCommand(inner, true);
            if (!out.empty()) combined += out + "\n";
        }
        if (!combined.empty() && combined.back() == '\n') combined.pop_back();
        return combined;
    });

    defineCommand("if", [](const std::string& rest, const Ctx&) -> std::string {
        auto condR = buildCondition(rest, "if");
        if (!condR.ok) return err(condR.error);
        std::string rem = condR.remaining;
        std::smatch m;
        static const std::regex trueRe(R"RX(^-\s*true\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, trueRe)) return err("if: missing \"-true=\" branch");
        rem = rem.substr(m.length());
        auto tr = parseBranch(rem, "if", "-true=");
        if (!tr.ok) return err(tr.error);
        rem = trim(rem.substr(tr.endPos));
        static const std::regex falseRe(R"RX(^-\s*false\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, falseRe)) return err("if: missing \"-false=\" branch");
        rem = rem.substr(m.length());
        auto fr = parseBranch(rem, "if", "-false=");
        if (!fr.ok) return err(fr.error);
        rem = trim(rem.substr(fr.endPos));
        if (!rem.empty()) return err("if: unexpected trailing text");
        if (tr.isNull && fr.isNull) return err("if: both branches cannot be (null)");
        bool cond = condR.check();
        auto& b = cond ? tr : fr;
        if (b.isNull) return "";
        return processCommand(b.cmd, true);
    }, { {"branch", R"RX(^-\s*true\s*=\s*\()RX"}, {"branch", R"RX(^-\s*false\s*=\s*\()RX"} });

    // functional["name"]-(c1)-(c2)-...  defines a function.
    // functional["name"]                calls it.
    // functional=["var"]                calls by name from a text variable.
    defineCommand("functional", [](const std::string& rest, const Ctx&) -> std::string {
        if (trim(rest).empty()) {
            if (functions.empty()) return hint("(no functions defined)");
            std::string out;
            for (auto& p : functions) out += "function: " + p.first + "\n";
            if (!out.empty() && out.back() == '\n') out.pop_back();
            return out;
        }
        std::smatch m;
        static const std::regex varCallRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*$)RX");
        static const std::regex callRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*$)RX", std::regex::icase);
        if (std::regex_search(rest, m, varCallRe)) {
            auto rv = readVarValue(m[1], "functional");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("functional: variable must be text");
            std::string ne = varNameError(rv.text);
            if (!ne.empty()) return err("functional: " + ne);
            if (!functions.count(rv.text)) return err("functional: function \"" + rv.text + "\" not found");
            executeSequentialCommands(functions[rv.text]);
            return "";
        }
        if (std::regex_search(rest, m, callRe)) {
            std::string ne = varNameError(m[1]);
            if (!ne.empty()) return err("functional: " + ne);
            if (!functions.count(m[1])) return err("functional: function \"" + m[1].str() + "\" not found");
            executeSequentialCommands(functions[m[1]]);
            return "";
        }
        static const std::regex defRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX");
        if (std::regex_search(rest, m, defRe)) {
            std::string ne = varNameError(m[1]);
            if (!ne.empty()) return err("functional: " + ne);
            std::string body = trim(m[2]);
            if (body.empty()) return err("functional: body is empty");
            auto subs = extractSubCommands(body);
            if (subs.empty()) return err("functional: body must be (cmd1)-(cmd2)-...");
            functions[m[1]] = subs;
            return "";
        }
        return err("functional: bad syntax");
    });

    defineCommand("functional-rename", [](const std::string& rest, const Ctx&) -> std::string {
        std::string r = trim(rest);
        if (r.empty()) return err("functional-rename: bad syntax");
        // Accepts literal "name", ["name"], or =["var"] for each of old and new.
        auto parseName = [](std::string& s, std::string& name) -> bool {
            std::smatch m;
            static const std::regex varRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\])RX");
            static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
            static const std::regex plainRe(R"RX(^"([^"]*)")RX");
            if (std::regex_search(s, m, varRe)) {
                auto rv = readVarValue(m[1], "functional-rename");
                if (!rv.ok) return false;
                name = rv.text;
                s = s.substr(m.length());
                return true;
            }
            if (std::regex_search(s, m, litRe)) { name = m[1]; s = s.substr(m.length()); return true; }
            if (std::regex_search(s, m, plainRe)) { name = m[1]; s = s.substr(m.length()); return true; }
            return false;
        };
        std::string oldName, newName;
        if (!parseName(r, oldName)) return err("functional-rename: expected old function name");
        r = trim(r);
        if (!parseName(r, newName)) return err("functional-rename: expected new function name");
        r = trim(r);
        if (!r.empty()) return err("functional-rename: unexpected trailing text");
        std::string e1 = varNameError(oldName);
        if (!e1.empty()) return err("functional-rename: " + e1);
        std::string e2 = varNameError(newName);
        if (!e2.empty()) return err("functional-rename: " + e2);
        if (!functions.count(oldName)) return err("functional-rename: function \"" + oldName + "\" not found");
        if (functions.count(newName)) return err("functional-rename: function \"" + newName + "\" already exists");
        functions[newName] = functions[oldName];
        functions.erase(oldName);
        return "";
    });

    defineCommand("functional-delete", [](const std::string& rest, const Ctx&) -> std::string {
        std::string t = trim(rest);
        if (t.empty()) return err("functional-delete: bad syntax");
        if (toLower(t) == "=all") { functions.clear(); return ""; }
        std::smatch m;
        static const std::regex varRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*$)RX");
        if (std::regex_search(t, m, varRe)) {
            auto rv = readVarValue(m[1], "functional-delete");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("functional-delete: variable must be text");
            std::string ne = varNameError(rv.text);
            if (!ne.empty()) return err("functional-delete: " + ne);
            if (!functions.count(rv.text)) return err("functional-delete: function \"" + rv.text + "\" not found");
            functions.erase(rv.text);
            return "";
        }
        std::string p = t;
        if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
        std::string ne = varNameError(p);
        if (!ne.empty()) return err("functional-delete: " + ne);
        if (!functions.count(p)) return err("functional-delete: function \"" + p + "\" not found");
        functions.erase(p);
        return "";
    });

    defineCommand("functional-replace", [](const std::string& rest, const Ctx&) -> std::string {
        std::string r = trim(rest);
        std::smatch m;
        std::string name;
        static const std::regex varRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*-\s*([\s\S]*)$)RX");
        if (std::regex_search(r, m, varRe)) {
            auto rv = readVarValue(m[1], "functional-replace");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("functional-replace: variable must be text");
            name = rv.text;
            r = trim(m[2]);
        } else if (std::regex_search(r, m, litRe)) {
            name = m[1];
            r = trim(m[2]);
        } else return err("functional-replace: bad syntax");
        std::string ne = varNameError(name);
        if (!ne.empty()) return err("functional-replace: " + ne);
        if (!functions.count(name)) return err("functional-replace: function \"" + name + "\" not found");
        if (r.empty()) return err("functional-replace: body is empty");
        auto subs = extractSubCommands(r);
        if (subs.empty()) return err("functional-replace: bad body");
        functions[name] = subs;
        return "";
    });

    // functional-exist["name"]-true=(cmd)-false=(cmd)
    // functional-exist["name"]-N"cmd" compares the N-th command text (1-based, exact match).
    defineCommand("functional-exist", [](const std::string& rest, const Ctx&) -> std::string {
        std::string rem = trim(rest);
        std::smatch m;
        std::string funcName;
        static const std::regex dynRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\])RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
        if (std::regex_search(rem, m, dynRe)) {
            auto rv = readVarValue(m[1], "functional-exist");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("functional-exist: variable must be text");
            funcName = rv.text;
            rem = trim(rem.substr(m.length()));
        } else if (std::regex_search(rem, m, litRe)) {
            funcName = m[1];
            rem = trim(rem.substr(m.length()));
        } else return err("functional-exist: expected [\"name\"] or =[\"var\"]");
        int cmpIndex = 0;
        std::string cmpText;
        static const std::regex tfPrefix(R"RX(^-\s*(?:true|false)\s*=)RX", std::regex::icase);
        if (!rem.empty() && !std::regex_search(rem, tfPrefix)) {
            static const std::regex idxRe(R"RX(^-\s*(\d+)\s*)RX");
            if (std::regex_search(rem, m, idxRe)) {
                cmpIndex = std::stoi(m[1]);
                rem = trim(rem.substr(m.length()));
            } else cmpIndex = 1;
            if (rem.empty() || rem[0] != '"') return err("functional-exist: expected a quoted command string");
            size_t j = 1;
            while (j < rem.size() && rem[j] != '"') {
                cmpText += rem[j];
                j++;
            }
            if (j >= rem.size()) return err("functional-exist: unterminated command string");
            rem = trim(rem.substr(j + 1));
            if (!rem.empty() && !std::regex_search(rem, tfPrefix)) return err("functional-exist: only one comparison string is allowed");
        }
        static const std::regex trueRe(R"RX(^-\s*true\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, trueRe)) return err("functional-exist: missing \"-true=\" branch");
        rem = rem.substr(m.length());
        auto tr = parseBranch(rem, "functional-exist", "-true=");
        if (!tr.ok) return err(tr.error);
        rem = trim(rem.substr(tr.endPos));
        static const std::regex falseRe(R"RX(^-\s*false\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, falseRe)) return err("functional-exist: missing \"-false=\" branch");
        rem = rem.substr(m.length());
        auto fr = parseBranch(rem, "functional-exist", "-false=");
        if (!fr.ok) return err(fr.error);
        rem = trim(rem.substr(fr.endPos));
        if (!rem.empty()) return err("functional-exist: unexpected trailing text");
        if (tr.isNull && fr.isNull) return err("functional-exist: both branches cannot be (null)");
        bool exists = functions.count(funcName) > 0;
        bool matched = exists;
        if (cmpIndex != 0) {
            matched = false;
            if (exists) {
                auto& body = functions[funcName];
                if (cmpIndex >= 1 && cmpIndex <= (int)body.size()) matched = (body[cmpIndex - 1] == cmpText);
            }
        }
        auto& b = matched ? tr : fr;
        if (b.isNull) return "";
        return processCommand(b.cmd, true);
    });

    // list["n"]-"item"(cmd)-...  creates a list with commands keyed by item names.
    // list["n"]"item"            runs the stored command.
    // list["n"]=["var"]          item name comes from a text variable.
    defineCommand("list", [](const std::string& rest, const Ctx&) -> std::string {
        if (trim(rest).empty()) {
            if (lists.empty()) return hint("(no lists defined)");
            std::string out;
            for (auto& p : lists) {
                out += "list: " + p.first + "\n";
                for (auto& q : p.second) out += "  " + q.first + " = " + q.second + "\n";
            }
            if (!out.empty() && out.back() == '\n') out.pop_back();
            return out;
        }
        std::smatch m;
        static const std::regex dynIdx(R"RX(^\[\s*"([^"]+)"\s*\]\s*=\s*\[\s*"([^"]+)"\s*\]\s*$)RX");
        if (std::regex_search(rest, m, dynIdx)) {
            std::string ln = m[1], vn = m[2];
            if (!lists.count(ln)) return err("list: list \"" + ln + "\" not found");
            std::string ne = varNameError(vn);
            if (!ne.empty()) return err("list: " + ne);
            if (!variables.count(vn)) return err("list: variable \"" + vn + "\" not found");
            std::string item = variables[vn].toString();
            if (!lists[ln].count(item)) return err("list: item \"" + item + "\" not found");
            return processCommand(lists[ln][item], true);
        }
        static const std::regex access(R"RX(^\[\s*"([^"]+)"\s*\]\s*"([^"]*)"\s*$)RX");
        if (std::regex_search(rest, m, access)) {
            std::string ln = m[1], item = m[2];
            if (!lists.count(ln)) return err("list: list \"" + ln + "\" not found");
            if (!lists[ln].count(item)) return err("list: item \"" + item + "\" not found");
            return processCommand(lists[ln][item], true);
        }
        static const std::regex create(R"RX(^\[\s*"([^"]+)"\s*\]([\s\S]*)$)RX", std::regex::icase);
        if (!std::regex_search(rest, m, create)) return err("list: bad syntax");
        std::string listName = m[1];
        std::string itemsPart = trim(m[2]);
        std::string ne = varNameError(listName);
        if (!ne.empty()) return err("list: " + ne);
        if (lists.count(listName)) return err("list: list already exists");
        if (itemsPart.empty()) { lists[listName] = {}; return ""; }
        std::map<std::string, std::string> items;
        auto matchItemName = [](const std::string& s, std::string& name, size_t& consumed) -> bool {
            std::smatch mm;
            static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
            static const std::regex litRe(R"RX(^"([^"]*)")RX");
            if (std::regex_search(s, mm, varRe)) {
                auto rv = readVarValue(mm[1], "list");
                if (!rv.ok) { name.clear(); return false; }
                name = rv.text;
                consumed = mm.length();
                return true;
            }
            if (std::regex_search(s, mm, litRe)) { name = mm[1]; consumed = mm.length(); return true; }
            return false;
        };
        while (!itemsPart.empty()) {
            itemsPart = trim(itemsPart);
            if (itemsPart.empty() || itemsPart[0] != '-') return err("list: expected \"-\" before item definition");
            itemsPart = trim(itemsPart.substr(1));
            std::string itemName;
            size_t consumed = 0;
            if (!matchItemName(itemsPart, itemName, consumed)) return err("list: expected item name");
            itemsPart = trim(itemsPart.substr(consumed));
            if (items.count(itemName)) return err("list: duplicate item \"" + itemName + "\"");
            if (itemsPart.empty() || itemsPart[0] != '(') return err("list: expected \"(\" after item name");
            auto p = extractParenthesized(itemsPart, 0);
            if (!p.found) return err("list: missing \")\"");
            items[itemName] = p.content;
            itemsPart = trim(itemsPart.substr(p.endPos));
        }
        lists[listName] = items;
        return "";
    });

    defineCommand("list-add", [](const std::string& rest, const Ctx&) -> std::string {
        std::string r = trim(rest);
        std::smatch m;
        std::string listName;
        static const std::regex dynRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*)RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
        if (std::regex_search(r, m, dynRe)) {
            auto rv = readVarValue(m[1], "list-add");
            if (!rv.ok) return err(rv.error);
            listName = rv.text;
            r = trim(r.substr(m.length()));
        } else if (std::regex_search(r, m, litRe)) {
            listName = m[1];
            r = trim(r.substr(m.length()));
        } else return err("list-add: bad syntax");
        if (!lists.count(listName)) return err("list-add: list \"" + listName + "\" not found");
        if (r.empty() || r[0] != '-') return err("list-add: expected \"-\" before item name");
        r = trim(r.substr(1));
        std::string itemName;
        size_t consumed = 0;
        {
            std::smatch mm;
            static const std::regex varRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
            static const std::regex litRe2(R"RX(^"([^"]*)")RX");
            if (std::regex_search(r, mm, varRe)) {
                auto rv = readVarValue(mm[1], "list-add");
                if (!rv.ok) return err(rv.error);
                itemName = rv.text;
                consumed = mm.length();
            } else if (std::regex_search(r, mm, litRe2)) {
                itemName = mm[1];
                consumed = mm.length();
            } else return err("list-add: expected item name");
        }
        r = trim(r.substr(consumed));
        if (lists[listName].count(itemName)) return err("list-add: item \"" + itemName + "\" already exists");
        if (r.empty() || r[0] != '(') return err("list-add: expected \"(\"");
        auto p = extractParenthesized(r, 0);
        if (!p.found) return err("list-add: missing \")\"");
        std::string after = trim(r.substr(p.endPos));
        if (!after.empty()) return err("list-add: unexpected trailing text");
        lists[listName][itemName] = p.content;
        return "";
    });

    defineCommand("list-delete", [](const std::string& rest, const Ctx&) -> std::string {
        std::string p = trim(rest);
        if (p.empty()) return err("list-delete: missing argument");
        if (toLower(p) == "=all") { lists.clear(); return ""; }
        std::smatch m;
        static const std::regex allRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*=\s*all\s*$)RX", std::regex::icase);
        if (std::regex_search(p, m, allRe)) {
            if (!lists.count(m[1])) return err("list-delete: list \"" + m[1].str() + "\" not found");
            lists[m[1]].clear();
            return "";
        }
        static const std::regex dynRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*=\s*\[\s*"([^"]+)"\s*\]\s*$)RX");
        if (std::regex_search(p, m, dynRe)) {
            std::string ln = m[1], vn = m[2];
            if (!lists.count(ln)) return err("list-delete: list \"" + ln + "\" not found");
            std::string ne = varNameError(vn);
            if (!ne.empty()) return err("list-delete: " + ne);
            if (!variables.count(vn)) return err("list-delete: variable \"" + vn + "\" not found");
            std::string item = variables[vn].toString();
            if (!lists[ln].count(item)) return err("list-delete: item \"" + item + "\" not found");
            lists[ln].erase(item);
            return "";
        }
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*"([^"]*)"\s*$)RX");
        if (std::regex_search(p, m, litRe)) {
            std::string ln = m[1], item = m[2];
            if (!lists.count(ln)) return err("list-delete: list \"" + ln + "\" not found");
            if (!lists[ln].count(item)) return err("list-delete: item \"" + item + "\" not found");
            lists[ln].erase(item);
            return "";
        }
        return err("list-delete: bad syntax");
    });

    defineCommand("list-rename", [](const std::string& rest, const Ctx&) -> std::string {
        std::string r = trim(rest);
        if (r.empty()) return err("list-rename: bad syntax");
        auto parseName = [](std::string& s, std::string& name) -> bool {
            std::smatch m;
            static const std::regex varRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\])RX");
            static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
            static const std::regex plainRe(R"RX(^"([^"]*)")RX");
            if (std::regex_search(s, m, varRe)) {
                auto rv = readVarValue(m[1], "list-rename");
                if (!rv.ok) return false;
                name = rv.text;
                s = s.substr(m.length());
                return true;
            }
            if (std::regex_search(s, m, litRe)) { name = m[1]; s = s.substr(m.length()); return true; }
            if (std::regex_search(s, m, plainRe)) { name = m[1]; s = s.substr(m.length()); return true; }
            return false;
        };
        std::string oldName, newName;
        if (!parseName(r, oldName)) return err("list-rename: expected old list name");
        r = trim(r);
        if (!parseName(r, newName)) return err("list-rename: expected new list name");
        r = trim(r);
        if (!r.empty()) return err("list-rename: unexpected trailing text");
        std::string e1 = varNameError(oldName);
        if (!e1.empty()) return err("list-rename: " + e1);
        std::string e2 = varNameError(newName);
        if (!e2.empty()) return err("list-rename: " + e2);
        if (!lists.count(oldName)) return err("list-rename: list \"" + oldName + "\" not found");
        if (lists.count(newName)) return err("list-rename: list \"" + newName + "\" already exists");
        lists[newName] = lists[oldName];
        lists.erase(oldName);
        return "";
    });

    // list-replace["n"]"old"-"new"(cmd): renames an item and replaces its command.
    defineCommand("list-replace", [](const std::string& rest, const Ctx&) -> std::string {
        std::string r = trim(rest);
        if (r.empty()) return err("list-replace: bad syntax");
        std::smatch m;
        std::string listName;
        static const std::regex varRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\]\s*)RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
        if (std::regex_search(r, m, varRe)) {
            auto rv = readVarValue(m[1], "list-replace");
            if (!rv.ok) return err(rv.error);
            listName = rv.text;
            r = trim(r.substr(m.length()));
        } else if (std::regex_search(r, m, litRe)) {
            listName = m[1];
            r = trim(r.substr(m.length()));
        } else return err("list-replace: expected list name");
        if (!lists.count(listName)) return err("list-replace: list \"" + listName + "\" not found");
        if (!r.empty() && r[0] == '-') r = trim(r.substr(1));
        auto matchItem = [](const std::string& s, std::string& name, size_t& consumed) -> bool {
            std::smatch mm;
            static const std::regex varRe2(R"RX(^\[\s*"([^"]+)"\s*\])RX");
            static const std::regex litRe2(R"RX(^"([^"]*)")RX");
            if (std::regex_search(s, mm, varRe2)) {
                auto rv = readVarValue(mm[1], "list-replace");
                if (!rv.ok) return false;
                name = rv.text;
                consumed = mm.length();
                return true;
            }
            if (std::regex_search(s, mm, litRe2)) { name = mm[1]; consumed = mm.length(); return true; }
            return false;
        };
        std::string oldName, newName;
        size_t consumed = 0;
        if (!matchItem(r, oldName, consumed)) return err("list-replace: expected old item name");
        r = trim(r.substr(consumed));
        if (r.empty() || r[0] != '-') return err("list-replace: expected \"-\"");
        r = trim(r.substr(1));
        if (!matchItem(r, newName, consumed)) return err("list-replace: expected new item name");
        r = trim(r.substr(consumed));
        if (!r.empty() && r[0] == '=') r = trim(r.substr(1));
        if (r.empty() || r[0] != '(') return err("list-replace: expected \"(\"");
        auto p = extractParenthesized(r, 0);
        if (!p.found) return err("list-replace: missing \")\"");
        std::string newCmd = p.content;
        std::string after = trim(r.substr(p.endPos));
        if (!after.empty()) return err("list-replace: unexpected trailing text");
        if (!lists[listName].count(oldName)) return err("list-replace: item \"" + oldName + "\" not found");
        if (newName != oldName && lists[listName].count(newName)) return err("list-replace: item \"" + newName + "\" already exists");
        lists[listName].erase(oldName);
        lists[listName][newName] = newCmd;
        return "";
    });

    // list-exist["name"]-true=(cmd)-false=(cmd)
    // list-exist["name"]"item"-true=(cmd)-false=(cmd)
    defineCommand("list-exist", [](const std::string& rest, const Ctx&) -> std::string {
        std::string rem = trim(rest);
        std::smatch m;
        std::string listName;
        static const std::regex dynRe(R"RX(^=\s*\[\s*"([^"]+)"\s*\])RX");
        static const std::regex litRe(R"RX(^\[\s*"([^"]+)"\s*\])RX");
        if (std::regex_search(rem, m, dynRe)) {
            auto rv = readVarValue(m[1], "list-exist");
            if (!rv.ok) return err(rv.error);
            if (!rv.isText) return err("list-exist: variable must be text");
            listName = rv.text;
            rem = trim(rem.substr(m.length()));
        } else if (std::regex_search(rem, m, litRe)) {
            listName = m[1];
            rem = trim(rem.substr(m.length()));
        } else return err("list-exist: expected [\"name\"] or =[\"var\"]");
        std::string itemName;
        bool hasItem = false;
        static const std::regex tfPrefix(R"RX(^-\s*(?:true|false)\s*=)RX", std::regex::icase);
        if (!rem.empty() && !std::regex_search(rem, tfPrefix)) {
            std::string rr = rem;
            if (!rr.empty() && rr[0] == '-') rr = trim(rr.substr(1));
            if (!rr.empty() && rr[0] == '[') {
                std::smatch mm;
                static const std::regex iVarRe(R"RX(^\[\s*"([^"]+)"\s*\]\s*)RX");
                if (!std::regex_search(rr, mm, iVarRe)) return err("list-exist: bad item variable reference");
                auto rv = readVarValue(mm[1], "list-exist");
                if (!rv.ok) return err(rv.error);
                itemName = rv.text;
                hasItem = true;
                rem = trim(rr.substr(mm.length()));
            } else if (!rr.empty() && rr[0] == '(') {
                auto p = extractParenthesized(rr, 0);
                if (!p.found) return err("list-exist: unbalanced parentheses");
                std::string result = processCommand(p.content, true);
                if (result.find("error:") != std::string::npos) return err("list-exist: item command failed: " + result);
                itemName = result;
                hasItem = true;
                rem = trim(rr.substr(p.endPos));
            } else if (!rr.empty() && rr[0] == '"') {
                std::smatch mm;
                static const std::regex iLitRe(R"RX(^"([^"]*)")RX");
                if (!std::regex_search(rr, mm, iLitRe)) return err("list-exist: unterminated item name");
                itemName = mm[1];
                hasItem = true;
                rem = trim(rr.substr(mm.length()));
            } else return err("list-exist: expected item name");
            if (!rem.empty() && !std::regex_search(rem, tfPrefix)) return err("list-exist: only one item name is allowed");
        }
        static const std::regex trueRe(R"RX(^-\s*true\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, trueRe)) return err("list-exist: missing \"-true=\" branch");
        rem = rem.substr(m.length());
        auto tr = parseBranch(rem, "list-exist", "-true=");
        if (!tr.ok) return err(tr.error);
        rem = trim(rem.substr(tr.endPos));
        static const std::regex falseRe(R"RX(^-\s*false\s*=\s*)RX", std::regex::icase);
        if (!std::regex_search(rem, m, falseRe)) return err("list-exist: missing \"-false=\" branch");
        rem = rem.substr(m.length());
        auto fr = parseBranch(rem, "list-exist", "-false=");
        if (!fr.ok) return err(fr.error);
        rem = trim(rem.substr(fr.endPos));
        if (!rem.empty()) return err("list-exist: unexpected trailing text");
        if (tr.isNull && fr.isNull) return err("list-exist: both branches cannot be (null)");
        bool exists = lists.count(listName) > 0;
        if (exists && hasItem) exists = lists[listName].count(itemName) > 0;
        auto& b = exists ? tr : fr;
        if (b.isNull) return "";
        return processCommand(b.cmd, true);
    });
}

// Central dispatcher. Cleans the raw input, finds the leading command name,
// and calls the matching handler with the rest of the string.
static std::string processCommand(const std::string& inputRaw, bool skipDisplay) {
    std::string input = sanitizeAscii(inputRaw);
    input = stripComments(input);
    input = trim(input);
    input = stripNewlinesOutsideStrings(input);
    input = trim(input);
    if (input.empty()) return "";
    if (!g_cmdRegexValid) buildCommandRegex();
    std::smatch m;
    if (!std::regex_match(input, m, g_cmdRegex)) {
        std::string word = input;
        size_t sp = word.find_first_of(" \t");
        if (sp != std::string::npos) word = word.substr(0, sp);
        return err("unknown command \"" + word + "\". Type \"help\" for commands.");
    }
    std::string name = toLower(m[1]);
    std::string rest = trim(m[2]);
    auto it = COMMANDS.find(name);
    if (it == COMMANDS.end()) return err("unknown command \"" + name + "\". Type \"help\" for commands.");
    Ctx ctx{ skipDisplay, name };
    return it->second(rest, ctx);
}

// Runs a list of sub-commands in order. If a sub-command is `time"N"-x`,
// the inner command is delayed; otherwise it runs inline.
static void executeSequentialCommands(const std::vector<std::string>& subcommands) {
    for (auto& cmd : subcommands) {
        static const std::regex timeRe(R"RX(^time\s*(?:"(\d+)"|\[\s*"([^"]+)"\s*\])\s*-\s*([\s\S]*)$)RX", std::regex::icase);
        std::smatch m;
        if (std::regex_search(cmd, m, timeRe)) {
            int delay = 0;
            bool okD = true;
            if (m[1].length()) delay = std::stoi(m[1]);
            else {
                std::string vn = m[2];
                if (!variables.count(vn)) {
                    rawOut(err("time: variable \"" + vn + "\" not found") + "\n");
                    okD = false;
                } else if (variables[vn].isText) {
                    rawOut(err("time: variable is not number") + "\n");
                    okD = false;
                } else {
                    double d = variables[vn].num;
                    if (d < 0 || d != std::floor(d)) {
                        rawOut(err("time: delay must be non-negative integer") + "\n");
                        okD = false;
                    } else delay = (int)d;
                }
            }
            std::string inner = trim(m[3]);
            if (okD && inner.empty()) {
                rawOut(err("time: missing command") + "\n");
                okD = false;
            }
            if (!okD) continue;
            scheduleDelayedCommand(delay, inner);
        } else {
            std::string out = processCommand(cmd, true);
            if (!out.empty()) rawOut(out + "\n");
        }
    }
}

// Main REPL loop. One "block" = everything typed until Enter.
// Ctrl+L inserts a newline so you can type a multi-line command; when you
// press Enter, the whole block is passed as ONE command to processCommand.
// Backspace at column 0 pulls the previous line back for editing.
static void doMainLoop() {
    enableAnsi();
    std::vector<std::string> cmdHistory;
    int histPos = -1;
    std::string histSavedInput;
    auto scrollScreen = [](int lines, bool older) {
        if (lines < 1) lines = 1;
        char buf[32];
        std::snprintf(buf, sizeof(buf), older ? "\033[%dT" : "\033[%dS", lines);
        rawOut(buf);
    };
    while (true) {
        std::string accum;
        std::string line;
        size_t cursor = 0;
        histPos = -1;
        RawMode raw;
        raw.on();
        rawOut("Script>");
        while (true) {
            std::string k = readKey();
            if (k.empty()) continue;
            if (k == "\n") {
                rawOut("\n");
                break;
            }
            if (k == "INSERT_NL") {
                accum += line + "\n";
                line.clear();
                cursor = 0;
                rawOut("\nScript> ");
                continue;
            }
            if (k == "CTRL_C") { raw.off(); rawOut("\n"); return; }
            if (k == "ESC") {
                accum.clear();
                line.clear();
                cursor = 0;
                rawOut("\r\033[KScript>");
                continue;
            }
            if (k == "BACKSPACE") {
                if (cursor > 0) {
                    line.erase(cursor - 1, 1);
                    cursor--;
                    rawOut("\r\033[KScript>" + line);
                    int back = (int)(line.size() - cursor);
                    if (back > 0) { char b[16]; std::snprintf(b, sizeof(b), "\033[%dD", back); rawOut(b); }
                } else if (!accum.empty()) {
                    size_t p = accum.size() >= 2 ? accum.rfind('\n', accum.size() - 2) : std::string::npos;
                    std::string prev = (p == std::string::npos)
                        ? accum.substr(0, accum.size() - 1)
                        : accum.substr(p + 1, accum.size() - p - 2);
                    accum = (p == std::string::npos) ? "" : accum.substr(0, p + 1);
                    line = prev;
                    cursor = line.size();
                    rawOut("\r\033[KScript>" + line);
                }
                continue;
            }
            if (k == "DEL") {
                if (cursor < line.size()) {
                    line.erase(cursor, 1);
                    rawOut("\r\033[KScript>" + line);
                    int back = (int)(line.size() - cursor);
                    if (back > 0) { char b[16]; std::snprintf(b, sizeof(b), "\033[%dD", back); rawOut(b); }
                }
                continue;
            }
            if (k == "LEFT") { if (cursor > 0) { cursor--; rawOut("\033[D"); } continue; }
            if (k == "RIGHT") { if (cursor < line.size()) { cursor++; rawOut("\033[C"); } continue; }
            if (k == "HOME") {
                if (cursor > 0) { char b[16]; std::snprintf(b, sizeof(b), "\033[%zuD", cursor); rawOut(b); cursor = 0; }
                continue;
            }
            if (k == "END") {
                if (cursor < line.size()) { char b[16]; std::snprintf(b, sizeof(b), "\033[%zuC", line.size() - cursor); rawOut(b); cursor = line.size(); }
                continue;
            }
            if (k == "CTRL_P") { scrollScreen(3, true); continue; }
            if (k == "CTRL_N") { scrollScreen(3, false); continue; }
            if (k == "PGUP") { scrollScreen(15, true); continue; }
            if (k == "PGDN") { scrollScreen(15, false); continue; }
            // Up/Down only walks history when the block is still a single line.
            if (k == "UP") {
                if (!accum.empty()) continue;
                if (cmdHistory.empty()) continue;
                if (histPos == -1) { histSavedInput = line; histPos = (int)cmdHistory.size() - 1; }
                else if (histPos > 0) histPos--;
                line = cmdHistory[histPos];
                cursor = line.size();
                rawOut("\r\033[KScript>" + line);
                continue;
            }
            if (k == "DOWN") {
                if (!accum.empty()) continue;
                if (histPos == -1) continue;
                histPos++;
                if (histPos >= (int)cmdHistory.size()) { histPos = -1; line = histSavedInput; }
                else { line = cmdHistory[histPos]; }
                cursor = line.size();
                rawOut("\r\033[KScript>" + line);
                continue;
            }
            if (k.size() == 1 && (unsigned char)k[0] >= 32) {
                line.insert(line.begin() + cursor, k[0]);
                cursor++;
                rawOut("\r\033[KScript>" + line);
                int back = (int)(line.size() - cursor);
                if (back > 0) { char b[16]; std::snprintf(b, sizeof(b), "\033[%dD", back); rawOut(b); }
                continue;
            }
        }
        raw.off();
        std::string full = accum + line;
        if (trim(full).empty()) continue;
        if (cmdHistory.empty() || cmdHistory.back() != full) cmdHistory.push_back(full);
        if (cmdHistory.size() > 500) cmdHistory.erase(cmdHistory.begin());
        histPos = -1;
        std::string out;
        try { out = processCommand(full, false); }
        catch (const std::exception& ex) { out = err(std::string("internal error: ") + ex.what()); }
        catch (...) { out = err("internal error"); }
        if (!out.empty()) rawOut(out + "\n");
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    initHelpText();
    setupCommands();
    rawOut("CompactScript\n");
    rawOut("Type 'help' for commands.\n");
    rawOut("Enter submits the whole block; Ctrl+L inserts a newline inside it.\n");
    rawOut("Ctrl+P/N: scroll | Up/Down: history | PgUp/PgDn: scroll 15 | Esc: clear block | Ctrl+C: quit\n\n");
    doMainLoop();
    return 0;
}