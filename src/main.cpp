#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <cstdint>   // uint64_t for FNV-1a
#include <iomanip>
#include <functional>
#include <optional>
#include <set>
#include <map>

namespace fs = std::filesystem;

// ============================================================
// CONSTANTS
// ============================================================

static const std::string ION_DIR      = ".ion";
static const std::string ION_HEAD     = ".ion/HEAD";
static const std::string ION_BRANCHES = ".ion/branches";
static const std::string ION_COMMITS  = ".ion/commits";
static const std::string ION_OBJECTS  = ".ion/objects/files";
static const std::string ION_IGNORE   = ".ionignore";

// ============================================================
// TERMINAL COLORS
// ============================================================

namespace color {
    const std::string reset   = "\033[0m";
    const std::string bold    = "\033[1m";
    const std::string red     = "\033[31m";
    const std::string green   = "\033[32m";
    const std::string yellow  = "\033[33m";
    const std::string cyan    = "\033[36m";
    const std::string gray    = "\033[90m";
    const std::string magenta = "\033[35m";
    const std::string white   = "\033[97m";
}

// ============================================================
// FILE I/O HELPERS
// ============================================================

// Read the first line of a file. Returns "" on failure.
std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::string value;
    std::getline(in, value);
    return value;
}

// Read all lines of a file into a vector.
std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

// Write a string to a file, creating parent directories as needed.
// Returns false and prints an error on failure.
bool write_file(const std::string& path, const std::string& value) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) {
        std::cerr << color::red << "error: cannot create directory for "
                  << path << ": " << ec.message() << color::reset << "\n";
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::cerr << color::red << "error: cannot write to " << path << color::reset << "\n";
        return false;
    }
    out << value;
    return true;
}

// Copy a file, creating parent directories. Returns false on failure.
bool copy_file_safe(const fs::path& src, const fs::path& dst,
                    fs::copy_options opts = fs::copy_options::overwrite_existing) {
    std::error_code ec;
    fs::path parent = dst.parent_path();
    if (!parent.empty() && parent != dst) {
        fs::create_directories(parent, ec);
        if (ec) {
            std::cerr << color::red << "error: cannot create directory "
                      << parent << ": " << ec.message() << color::reset << "\n";
            return false;
        }
    }
    fs::copy_file(src, dst, opts, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot copy " << src
                  << " -> " << dst << ": " << ec.message() << color::reset << "\n";
        return false;
    }
    return true;
}

// ============================================================
// REPO VALIDATION
// ============================================================

bool is_valid_repo() {
    return fs::exists(ION_HEAD)     &&
           fs::exists(ION_BRANCHES) &&
           fs::exists(ION_COMMITS)  &&
           fs::exists(ION_OBJECTS);
}

int repo_error() {
    std::cerr << color::red
              << "error: not an ion repository. Run 'ion init' first.\n"
              << color::reset;
    return 1;
}

// ============================================================
// BRANCH HELPERS
// ============================================================

std::string get_current_branch() {
    return read_file(ION_HEAD);
}

std::string get_branch_commit(const std::string& branch) {
    std::string path = ION_BRANCHES + "/" + branch;
    if (!fs::exists(path)) return "null";
    std::string val = read_file(path);
    return val.empty() ? "null" : val;
}

bool set_branch_commit(const std::string& branch, const std::string& commit) {
    return write_file(ION_BRANCHES + "/" + branch, commit);
}

// ============================================================
// HASHING  —  FNV-1a 64-bit
//
// Replaces std::hash<std::string> which is:
//   - implementation-defined (no guarantee of cross-platform consistency)
//   - 32-bit on 32-bit targets (different hash length, incompatible repos)
//   - not a stable algorithm across compilers or stdlib versions
//
// FNV-1a 64-bit is:
//   - fully specified and deterministic on every conforming C++ compiler
//   - always produces 16 hex characters regardless of platform word size
//   - no external dependencies
// ============================================================

// Returns a 16-character lowercase hex hash, or "" on I/O error.
// An empty return must be treated as a fatal error by the caller.
std::string hash_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "";

    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME        = 1099511628211ULL;

    uint64_t h = FNV_OFFSET_BASIS;
    char c;
    while (in.get(c)) {
        h ^= static_cast<uint8_t>(c);
        h *= FNV_PRIME;
    }
    if (in.bad()) return ""; // distinguish I/O error from clean EOF

    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << h;
    return hex.str();
}

// ============================================================
// PATH NORMALIZATION
// ============================================================

static std::string norm(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

// ============================================================
// INPUT VALIDATION
//
// All user-supplied names (branch names, commit IDs) are validated
// before being concatenated onto filesystem paths.  This prevents
// path traversal attacks such as:
//   ion checkout ../../etc/passwd
//   ion show ../HEAD
// ============================================================

// Safe branch name: letters, digits, '-', '_', '.'.
// Must not start with '.' and must not contain '..'.
bool is_safe_branch_name(const std::string& name) {
    if (name.empty()) return false;
    if (name[0] == '.') return false;                          // no hidden / relative
    if (name.find("..") != std::string::npos) return false;   // no traversal
    for (char raw : name)
        if (!std::isalnum(static_cast<unsigned char>(raw)) && raw != '_' && raw != '-' && raw != '.') return false;
    return true;
}

// Safe commit ID: non-empty, digits only (ion commit IDs are sequential integers).
bool is_safe_commit_id(const std::string& id) {
    if (id.empty()) return false;
    for (char raw : id)
        if (!std::isdigit(static_cast<unsigned char>(raw))) return false;
    return true;
}

// ============================================================
// .IONIGNORE
// ============================================================

struct IgnoreRules {
    std::vector<std::string> extensions;   // e.g. ".log"
    std::vector<std::string> dir_prefixes; // e.g. "build/"
    std::vector<std::string> filenames;    // exact relative path or bare filename
};

static std::string trim_str(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

IgnoreRules load_ignore_rules() {
    IgnoreRules rules;
    if (!fs::exists(ION_IGNORE)) return rules;
    for (auto& raw : read_lines(ION_IGNORE)) {
        std::string line = trim_str(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > 2 && line[0] == '*' && line[1] == '.') {
            rules.extensions.push_back(line.substr(1)); // store as ".log"
        } else if (line.back() == '/') {
            rules.dir_prefixes.push_back(line);
        } else {
            rules.filenames.push_back(line);
        }
    }
    return rules;
}

bool should_ignore(const std::string& rel_raw, const IgnoreRules& rules) {
    std::string rel = norm(rel_raw);

    // Always ignore .ion internals.
    if (rel == ".ion" || rel.rfind(".ion/", 0) == 0) return true;

    for (auto& ext : rules.extensions) {
        if (rel.size() >= ext.size() &&
            rel.compare(rel.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    for (auto& prefix : rules.dir_prefixes) {
        std::string dir = prefix.substr(0, prefix.size() - 1); // strip trailing /
        if (rel == dir || rel.rfind(dir + "/", 0) == 0) return true;
    }
    for (auto& fname : rules.filenames) {
        if (rel == fname) return true;
        if (fs::path(rel).filename().string() == fname) return true;
    }
    return false;
}

// ============================================================
// COMMIT SERIALIZATION HELPERS
//
// Problem (v0.3): commit messages written verbatim.  A message containing
// a real newline injects fake header lines into the commit file, making
// the parser read a forged "parent:" or "files:" entry.
//
// Fix: escape before write, unescape after read.
//   '\' -> '\\'
//   newline -> '\n'   (two printable characters)
//   carriage return -> '\r'
// ============================================================

static std::string escape_message(const std::string& msg) {
    std::string out;
    out.reserve(msg.size());
    for (char c : msg) {
        if      (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n";  }
        else if (c == '\r') { out += "\\r";  }
        else                { out += c;      }
    }
    return out;
}

static std::string unescape_message(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            ++i;
            if      (raw[i] == 'n')  { out += '\n'; }
            else if (raw[i] == 'r')  { out += '\r'; }
            else if (raw[i] == '\\') { out += '\\'; }
            else { out += '\\'; out += raw[i]; } // unknown escape — preserve as-is
        } else {
            out += raw[i];
        }
    }
    return out;
}

// ============================================================
// COMMIT STRUCT + SERIALIZATION
//
// Commit file format (v0.4):
//
//   id: <id>
//   parent: <parent>
//   timestamp: <unix_epoch>
//   message: <escaped_single_line>
//   files:
//   \t<16hexchars> <filename to end of line>
//
// The hash field is always exactly 16 lowercase hex characters.
// A single space separates hash from filename.
// The filename extends to end-of-line, so filenames with spaces work.
//
// Why this format:
//   v0.3 used "filename hash" separated by whitespace (iss >> f >> h).
//   Any filename containing a space silently truncated the filename and
//   discarded the hash, making restore impossible.
// ============================================================

struct Commit {
    std::string id;
    std::string parent;
    std::string message;
    std::string timestamp;
    std::unordered_map<std::string, std::string> files; // rel_path -> hash
};

Commit read_commit(const std::string& id) {
    Commit c;
    c.id     = id;
    c.parent = "null";
    if (id == "null" || id.empty()) return c;

    std::string path = ION_COMMITS + "/" + id;
    if (!fs::exists(path)) return c;

    std::ifstream in(path);
    if (!in) {
        // Fail loudly — a commit we can't open is a defect, not an empty record.
        std::cerr << color::red << "error: cannot open commit " << id
                  << color::reset << "\n";
        return c;
    }

    std::string line;
    bool in_files = false;

    while (std::getline(in, line)) {
        if (!in_files) {
            if      (line.rfind("id: ", 0) == 0)        c.id        = line.substr(4);
            else if (line.rfind("parent: ", 0) == 0)    c.parent    = line.substr(8);
            else if (line.rfind("message: ", 0) == 0)   c.message   = unescape_message(line.substr(9));
            else if (line.rfind("timestamp: ", 0) == 0) c.timestamp = line.substr(11);
            else if (line == "files:")                   in_files    = true;
        } else {
            // File entry: TAB + 16-char-hash + SPACE + filename
            // Minimum valid length: 1('\t') + 16(hash) + 1(' ') + 1(name) = 19
            if (line.size() < 19) continue;
            if (line[0] != '\t') continue;
            if (line[17] != ' ') continue; // malformed — skip
            std::string hash     = line.substr(1, 16);
            std::string filename = line.substr(18); // rest of line, spaces preserved
            if (!hash.empty() && !filename.empty())
                c.files[filename] = hash;
        }
    }
    return c;
}

bool write_commit(const Commit& c) {
    std::error_code ec;
    fs::create_directories(ION_COMMITS, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot create commits directory: "
                  << ec.message() << color::reset << "\n";
        return false;
    }

    std::string path = ION_COMMITS + "/" + c.id;
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::cerr << color::red << "error: cannot write commit " << c.id
                  << color::reset << "\n";
        return false;
    }

    out << "id: "        << c.id                      << "\n"
        << "parent: "    << c.parent                  << "\n"
        << "timestamp: " << c.timestamp               << "\n"
        << "message: "   << escape_message(c.message) << "\n"
        << "files:\n";

    // Sort entries for deterministic output.
    std::vector<std::pair<std::string, std::string>> sorted(c.files.begin(), c.files.end());
    std::sort(sorted.begin(), sorted.end());
    for (auto& [f, h] : sorted)
        out << "\t" << h << " " << f << "\n";

    return true;
}

// ============================================================
// COMMIT ID GENERATION
// ============================================================

std::string next_commit_id() {
    int max_id = 0;
    if (fs::exists(ION_COMMITS)) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(ION_COMMITS, ec)) {
            try {
                int n = std::stoi(entry.path().filename().string());
                if (n > max_id) max_id = n;
            } catch (...) {}
        }
    }
    return std::to_string(max_id + 1);
}

// ============================================================
// WORKING DIRECTORY SCAN
// ============================================================

// Returns nullopt and prints an error if the directory cannot be scanned
// or if any file cannot be read.  The caller MUST treat nullopt as fatal
// and abort the current command — never assume an empty map on error.
std::optional<std::unordered_map<std::string, std::string>>
collect_working_files(const IgnoreRules& rules) {
    std::unordered_map<std::string, std::string> result;
    std::error_code ec;

    // Construct the iterator explicitly so we can check construction failure.
    auto it = fs::recursive_directory_iterator(
        ".", fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot scan working directory: "
                  << ec.message() << color::reset << "\n";
        return std::nullopt;
    }

    for (const auto& entry : it) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (should_ignore(rel, rules)) continue;

        std::string h = hash_file(entry.path());
        if (h.empty()) {
            // hash_file returns "" on any I/O error — this is fatal.
            std::cerr << color::red << "error: cannot read file: " << rel
                      << color::reset << "\n";
            return std::nullopt;
        }
        result[rel] = h;
    }
    return result;
}

// Collect only ignored files (for status --ignored display).
// Non-fatal: if the scan fails, returns an empty list — it's display-only.
std::vector<std::string>
collect_ignored_files(const IgnoreRules& rules) {
    std::vector<std::string> result;
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        ".", fs::directory_options::skip_permission_denied, ec);
    if (ec) return result;

    for (const auto& entry : it) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (rel.rfind(".ion/", 0) == 0 || rel == ".ion") continue;
        if (should_ignore(rel, rules)) result.push_back(rel);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================
// SAFE RESTORE — PRE-FLIGHT CHECK
//
// Before any destructive restore, determine which working-directory files
// would be permanently lost.
//
// A file is "at risk" when ALL of the following are true:
//   1. It exists in the working directory.
//   2. Its current content differs from what the current HEAD commit has
//      (i.e. it has unsaved local changes, OR it is untracked).
//   3. The target commit does NOT restore it to its exact current content.
//
// If the at-risk list is non-empty the caller must abort — ion never
// silently destroys work the user has not saved.
// ============================================================

std::vector<std::string> files_at_risk(
    const std::unordered_map<std::string, std::string>& working,
    const std::unordered_map<std::string, std::string>& head_committed,
    const std::unordered_map<std::string, std::string>& target_committed)
{
    std::vector<std::string> at_risk;
    for (auto& [f, wh] : working) {
        // Does the working copy exactly match HEAD? If yes, it's clean.
        auto hit = head_committed.find(f);
        bool matches_head = (hit != head_committed.end() && hit->second == wh);
        if (matches_head) continue;

        // File has unsaved content.  Will the restore preserve it exactly?
        auto tit = target_committed.find(f);
        bool preserved = (tit != target_committed.end() && tit->second == wh);
        if (!preserved) at_risk.push_back(f);
    }
    std::sort(at_risk.begin(), at_risk.end());
    return at_risk;
}

// ============================================================
// RESTORE WORKING DIRECTORY
// ============================================================

bool restore_commit(const std::string& id, const IgnoreRules& rules) {
    // Phase 1: collect the list of files to remove.
    std::vector<fs::path> to_remove;
    std::error_code ec;
    auto it = fs::recursive_directory_iterator(
        ".", fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot scan working directory: "
                  << ec.message() << color::reset << "\n";
        return false;
    }
    for (const auto& entry : it) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (!should_ignore(rel, rules)) to_remove.push_back(entry.path());
    }

    // Phase 2: remove them.
    for (auto& p : to_remove) {
        fs::remove(p, ec);
        if (ec) {
            std::cerr << color::red << "error: cannot remove " << p
                      << ": " << ec.message() << color::reset << "\n";
            return false;
        }
    }

    if (id == "null" || id.empty()) return true;

    // Phase 3: restore files from the target commit.
    Commit c = read_commit(id);
    bool ok  = true;
    for (auto& [file, hash] : c.files) {
        fs::path src = ION_OBJECTS + "/" + hash;
        if (!fs::exists(src)) {
            std::cerr << color::red << "error: missing object " << hash
                      << " for file " << file << color::reset << "\n";
            ok = false;
            continue;
        }
        if (!copy_file_safe(src, fs::path(file))) ok = false;
    }
    return ok;
}

// ============================================================
// TIMESTAMP FORMATTING
// ============================================================

std::string format_timestamp(const std::string& ts_str) {
    if (ts_str.empty()) return "unknown";
    try {
        time_t ts = static_cast<time_t>(std::stoll(ts_str));
        std::tm* t = std::localtime(&ts);
        if (!t) return ts_str; // null-guard: localtime returns nullptr for out-of-range values
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        return buf;
    } catch (...) { return ts_str; }
}

// ============================================================
// DIFF ENGINE
// ============================================================

struct DiffLine { char op; std::string text; }; // op: ' ', '+', '-'

// LCS-based unified line diff.
std::vector<DiffLine> compute_diff(const std::vector<std::string>& old_lines,
                                   const std::vector<std::string>& new_lines) {
    size_t n = old_lines.size(), m = new_lines.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j)
            dp[i][j] = (old_lines[i-1] == new_lines[j-1])
                ? dp[i-1][j-1] + 1
                : std::max(dp[i-1][j], dp[i][j-1]);

    std::vector<DiffLine> result;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && old_lines[i-1] == new_lines[j-1]) {
            result.push_back({' ', old_lines[i-1]}); --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            result.push_back({'+', new_lines[j-1]}); --j;
        } else {
            result.push_back({'-', old_lines[i-1]}); --i;
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// Print a file diff with ±context lines around changed hunks.
void print_file_diff(const std::string& filename,
                     const std::string& old_hash,
                     const std::string& new_hash,
                     int context = 2) {
    std::vector<std::string> old_lines, new_lines;
    if (old_hash != "null" && fs::exists(ION_OBJECTS + "/" + old_hash))
        old_lines = read_lines(ION_OBJECTS + "/" + old_hash);
    if (new_hash != "null" && fs::exists(filename))
        new_lines = read_lines(filename);

    auto diff = compute_diff(old_lines, new_lines);

    bool has_changes = false;
    for (auto& d : diff) if (d.op != ' ') { has_changes = true; break; }
    if (!has_changes) return;

    std::cout << color::bold << color::cyan << "diff " << filename << color::reset << "\n";

    std::vector<size_t> changed;
    for (size_t k = 0; k < diff.size(); ++k)
        if (diff[k].op != ' ') changed.push_back(k);

    std::vector<std::pair<size_t, size_t>> ranges;
    for (size_t ci : changed) {
        size_t ctx = static_cast<size_t>(context);
        size_t lo = (ci >= ctx) ? ci - ctx : 0;
        size_t hi = std::min(ci + ctx + 1, diff.size());
        if (!ranges.empty() && lo <= ranges.back().second)
            ranges.back().second = hi;
        else
            ranges.push_back({lo, hi});
    }

    for (auto& [lo, hi] : ranges) {
        std::cout << color::gray << "@@ +" << lo << " @@" << color::reset << "\n";
        for (size_t k = lo; k < hi; ++k) {
            auto& d = diff[k];
            if      (d.op == '+') std::cout << color::green << "+ " << d.text << color::reset << "\n";
            else if (d.op == '-') std::cout << color::red   << "- " << d.text << color::reset << "\n";
            else                  std::cout << color::gray  << "  " << d.text << color::reset << "\n";
        }
    }
}

// ============================================================
// STATUS CLASSIFICATION
// ============================================================

struct StatusResult {
    std::vector<std::string> modified;
    std::vector<std::string> added;
    std::vector<std::string> deleted;
    std::vector<std::string> ignored;
};

StatusResult compute_status(
    const std::unordered_map<std::string, std::string>& committed,
    const std::unordered_map<std::string, std::string>& working,
    const std::vector<std::string>& ignored_files)
{
    StatusResult s;
    s.ignored = ignored_files;

    for (auto& [f, h] : working) {
        auto it = committed.find(f);
        if (it == committed.end())   s.added.push_back(f);
        else if (it->second != h)    s.modified.push_back(f);
    }
    for (auto& [f, _] : committed)
        if (working.find(f) == working.end()) s.deleted.push_back(f);

    std::sort(s.modified.begin(), s.modified.end());
    std::sort(s.added.begin(),    s.added.end());
    std::sort(s.deleted.begin(),  s.deleted.end());
    return s;
}

bool has_changes(const StatusResult& s) {
    return !s.modified.empty() || !s.added.empty() || !s.deleted.empty();
}

// ============================================================
// PRINT HELPERS
// ============================================================

static void print_separator() {
    std::cout << color::gray
              << "────────────────────────────────────────\n"
              << color::reset;
}

static void print_commit_header(const Commit& c, bool is_head = false) {
    std::cout << color::bold << color::yellow << "commit " << c.id << color::reset;
    if (is_head) std::cout << color::cyan << "  <-- HEAD" << color::reset;
    std::cout << "\n";
    std::cout << color::gray << "  date:    " << format_timestamp(c.timestamp)
              << color::reset << "\n";
    std::cout << color::bold << "  message: " << c.message << color::reset << "\n";
}

static void print_change_summary(const StatusResult& s) {
    for (auto& f : s.modified)
        std::cout << "  " << color::yellow << "~ " << f << color::reset << "\n";
    for (auto& f : s.added)
        std::cout << "  " << color::green  << "+ " << f << color::reset << "\n";
    for (auto& f : s.deleted)
        std::cout << "  " << color::red    << "- " << f << color::reset << "\n";
}

static std::string change_stats(const StatusResult& s) {
    std::ostringstream ss;
    int total = (int)(s.modified.size() + s.added.size() + s.deleted.size());
    ss << total << " file(s) changed  "
       << "(+" << s.added.size()
       << " ~" << s.modified.size()
       << " -" << s.deleted.size() << ")";
    return ss.str();
}

// ============================================================
// CMD: INIT
// ============================================================

int cmd_init() {
    if (fs::exists(ION_DIR)) {
        std::cout << color::yellow << "Repository already exists.\n" << color::reset;
        return 0;
    }

    // Check each directory creation independently — a shared error_code variable
    // would be silently reset to success by a later successful call.
    std::error_code ec;

    fs::create_directories(ION_COMMITS, ec);
    if (ec) {
        std::cerr << color::red << "error: failed to create commits directory: "
                  << ec.message() << color::reset << "\n";
        return 1;
    }
    fs::create_directories(ION_OBJECTS, ec);
    if (ec) {
        std::cerr << color::red << "error: failed to create objects directory: "
                  << ec.message() << color::reset << "\n";
        return 1;
    }
    fs::create_directories(ION_BRANCHES, ec);
    if (ec) {
        std::cerr << color::red << "error: failed to create branches directory: "
                  << ec.message() << color::reset << "\n";
        return 1;
    }

    if (!write_file(ION_HEAD,               "main")) return 1;
    if (!write_file(ION_BRANCHES + "/main", "null")) return 1;

    std::cout << color::green << color::bold << "Initialized ion repository.\n" << color::reset;
    std::cout << color::gray
              << "  branch:   main\n"
              << "  location: " << fs::absolute(ION_DIR).string() << "\n"
              << color::reset;
    return 0;
}

// ============================================================
// CMD: STATUS
// ============================================================

int cmd_status(bool show_ignored = false) {
    if (!is_valid_repo()) return repo_error();

    IgnoreRules rules       = load_ignore_rules();
    std::string branch      = get_current_branch();
    std::string head_commit = get_branch_commit(branch);

    std::cout << color::bold << "On branch " << color::cyan << branch << color::reset;
    if (head_commit == "null")
        std::cout << color::gray << "  (no commits yet)" << color::reset;
    else
        std::cout << color::gray << "  (head: " << head_commit << ")" << color::reset;
    std::cout << "\n\n";

    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") committed = read_commit(head_commit).files;

    auto working_opt = collect_working_files(rules);
    if (!working_opt) return 1;
    auto& working = *working_opt;

    auto ignored_list = show_ignored ? collect_ignored_files(rules) : std::vector<std::string>{};
    auto s = compute_status(committed, working, ignored_list);

    if (!has_changes(s) && (!show_ignored || s.ignored.empty())) {
        std::cout << color::green << "Nothing to save, working directory clean.\n" << color::reset;
        return 0;
    }

    if (!s.modified.empty()) {
        std::cout << color::bold << "Modified:\n" << color::reset;
        for (auto& f : s.modified)
            std::cout << "  " << color::yellow << "~ " << f << color::reset << "\n";
        std::cout << "\n";
    }
    if (!s.added.empty()) {
        std::cout << color::bold << "Untracked:\n" << color::reset;
        for (auto& f : s.added)
            std::cout << "  " << color::green << "+ " << f << color::reset << "\n";
        std::cout << "\n";
    }
    if (!s.deleted.empty()) {
        std::cout << color::bold << "Deleted:\n" << color::reset;
        for (auto& f : s.deleted)
            std::cout << "  " << color::red << "- " << f << color::reset << "\n";
        std::cout << "\n";
    }
    if (show_ignored && !s.ignored.empty()) {
        std::cout << color::bold << "Ignored:\n" << color::reset;
        for (auto& f : s.ignored)
            std::cout << "  " << color::gray << "  " << f << color::reset << "\n";
        std::cout << "\n";
    }

    if (has_changes(s))
        std::cout << color::gray << change_stats(s) << "\n" << color::reset;

    return 0;
}

// ============================================================
// CMD: DIFF
// ============================================================

int cmd_diff(const std::string& target_file = "") {
    if (!is_valid_repo()) return repo_error();

    IgnoreRules rules       = load_ignore_rules();
    std::string branch      = get_current_branch();
    std::string head_commit = get_branch_commit(branch);

    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") committed = read_commit(head_commit).files;

    auto working_opt = collect_working_files(rules);
    if (!working_opt) return 1;
    auto& working = *working_opt;

    // Normalize the target path for comparison so "ion diff ./foo.txt" works.
    std::string norm_target = norm(target_file);

    std::set<std::string> all_files;
    for (auto& [f, _] : working)   all_files.insert(f);
    for (auto& [f, _] : committed) all_files.insert(f);

    bool any_diff = false;

    for (auto& file : all_files) {
        if (!norm_target.empty() && norm(file) != norm_target) continue;

        std::string old_hash = "null", new_hash = "null";
        auto ci = committed.find(file); if (ci != committed.end()) old_hash = ci->second;
        auto wi = working.find(file);   if (wi != working.end())   new_hash = wi->second;

        if (old_hash == new_hash) continue;
        any_diff = true;

        if (old_hash == "null") {
            std::cout << color::bold << color::green << "+++ " << file
                      << color::gray << " (new file)" << color::reset << "\n";
            for (auto& l : read_lines(file))
                std::cout << color::green << "+ " << l << color::reset << "\n";
            std::cout << "\n";
        } else if (new_hash == "null") {
            std::cout << color::bold << color::red << "--- " << file
                      << color::gray << " (deleted)" << color::reset << "\n\n";
        } else {
            print_file_diff(file, old_hash, new_hash);
            std::cout << "\n";
        }
    }

    if (!any_diff)
        std::cout << color::gray << "No differences from last commit.\n" << color::reset;

    return 0;
}

// ============================================================
// CMD: SAVE
//
// v0.4 change: the --confirm interactive prompt has been removed.
// ion's philosophy requires no hidden interaction — the user issues
// a command and it executes.  Confirmation is the user's responsibility
// before running the command (e.g. run 'ion status' first).
// ============================================================

int cmd_save(const std::string& message) {
    if (!is_valid_repo()) return repo_error();
    if (message.empty()) {
        std::cerr << color::red << "error: save message cannot be empty\n" << color::reset;
        return 1;
    }

    IgnoreRules rules  = load_ignore_rules();
    std::string branch = get_current_branch();
    std::string parent = get_branch_commit(branch);

    std::unordered_map<std::string, std::string> committed;
    if (parent != "null") committed = read_commit(parent).files;

    auto working_opt = collect_working_files(rules);
    if (!working_opt) return 1;
    auto& working = *working_opt;

    auto s = compute_status(committed, working, {});

    if (!has_changes(s)) {
        std::cout << color::yellow
                  << "Nothing to save — working directory matches last commit.\n"
                  << color::reset;
        return 0;
    }

    std::cout << color::bold << "Changes to save:\n" << color::reset;
    print_change_summary(s);
    std::cout << "\n";

    // Store content objects.
    std::error_code ec;
    fs::create_directories(ION_OBJECTS, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot create objects directory: "
                  << ec.message() << color::reset << "\n";
        return 1;
    }
    for (auto& [rel, hash] : working) {
        fs::path obj = ION_OBJECTS + "/" + hash;
        if (!fs::exists(obj))
            if (!copy_file_safe(fs::path(rel), obj, fs::copy_options::skip_existing))
                return 1;
    }

    // Build and write commit.
    Commit c;
    c.id        = next_commit_id();
    c.parent    = parent;
    c.message   = message;
    c.timestamp = std::to_string(std::time(nullptr));
    c.files     = working;

    if (!write_commit(c))                 return 1;
    if (!set_branch_commit(branch, c.id)) return 1;

    std::cout << color::green << color::bold << "Saved snapshot " << c.id
              << color::reset << color::gray << " on branch " << branch << color::reset << "\n";
    std::cout << color::gray << "  " << change_stats(s) << "\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: BRANCH (CREATE)
// ============================================================

int cmd_branch(const std::string& name) {
    if (!is_valid_repo()) return repo_error();
    if (!is_safe_branch_name(name)) {
        std::cerr << color::red
                  << "error: invalid branch name '" << name << "'\n"
                  << "  Allowed: letters, digits, '-', '_', '.'\n"
                  << "  Must not start with '.' or contain '..'\n"
                  << color::reset;
        return 1;
    }

    fs::path path = ION_BRANCHES + "/" + name;
    if (fs::exists(path)) {
        std::cerr << color::red << "error: branch '" << name << "' already exists\n"
                  << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    std::string commit  = get_branch_commit(current);
    if (!write_file(path.string(), commit)) return 1;

    std::cout << color::green << "Created branch " << color::bold << name << color::reset << "\n";
    std::cout << color::gray << "  from: " << current;
    if (commit != "null") std::cout << " at commit " << commit;
    std::cout << "\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: BRANCH-DELETE
// ============================================================

int cmd_branch_delete(const std::string& name) {
    if (!is_valid_repo()) return repo_error();
    if (!is_safe_branch_name(name)) {
        std::cerr << color::red << "error: invalid branch name '" << name << "'\n"
                  << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    if (name == current) {
        std::cerr << color::red
                  << "error: cannot delete the currently active branch '" << name << "'\n"
                  << color::gray << "  Switch to another branch first: ion switch <branch>\n"
                  << color::reset;
        return 1;
    }

    fs::path path = ION_BRANCHES + "/" + name;
    if (!fs::exists(path)) {
        std::cerr << color::red << "error: branch '" << name << "' not found\n"
                  << color::reset;
        return 1;
    }

    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        std::cerr << color::red << "error: cannot delete branch '" << name
                  << "': " << ec.message() << color::reset << "\n";
        return 1;
    }

    std::cout << color::green << "Deleted branch " << color::bold << name << color::reset << "\n";
    std::cout << color::gray << "  Commits and objects are preserved.\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: BRANCHES (LIST)
// ============================================================

int cmd_branches() {
    if (!is_valid_repo()) return repo_error();

    std::string current = get_current_branch();
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(ION_BRANCHES, ec))
        names.push_back(entry.path().filename().string());
    if (ec) {
        std::cerr << color::red << "error: cannot read branches directory: "
                  << ec.message() << color::reset << "\n";
        return 1;
    }
    std::sort(names.begin(), names.end());

    for (auto& name : names) {
        std::string commit = get_branch_commit(name);
        if (name == current)
            std::cout << color::green << color::bold << "* " << name << color::reset;
        else
            std::cout << "  " << color::cyan << name << color::reset;

        if (commit == "null") {
            std::cout << color::gray << "  (no commits)" << color::reset;
        } else {
            std::cout << color::gray << "  -> " << commit;
            Commit c = read_commit(commit);
            if (!c.message.empty()) {
                std::string snippet = c.message.size() > 48
                    ? c.message.substr(0, 48) + "…"
                    : c.message;
                std::cout << "  \"" << snippet << "\"";
            }
            std::cout << color::reset;
        }
        std::cout << "\n";
    }
    return 0;
}

// ============================================================
// CMD: CHECKOUT  (also dispatched by 'switch' alias)
//
// v0.4 change: the interactive "Continue anyway? [y/N]" prompt is removed.
// If checkout would destroy unsaved work, the command fails with a clear
// error listing every at-risk file.  The user must save or resolve first.
// ============================================================

int cmd_checkout(const std::string& name) {
    if (!is_valid_repo()) return repo_error();
    if (!is_safe_branch_name(name)) {
        std::cerr << color::red
                  << "error: invalid branch name '" << name << "'\n"
                  << "  Allowed: letters, digits, '-', '_', '.'\n"
                  << color::reset;
        return 1;
    }

    fs::path path = ION_BRANCHES + "/" + name;
    if (!fs::exists(path)) {
        std::cerr << color::red << "error: branch '" << name << "' not found\n"
                  << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    if (name == current) {
        std::cout << color::yellow << "Already on branch '" << name << "'\n" << color::reset;
        return 0;
    }

    IgnoreRules rules = load_ignore_rules();

    // Resolve all three states needed for the pre-flight check.
    std::string head_id = get_branch_commit(current);
    std::unordered_map<std::string, std::string> head_committed;
    if (head_id != "null") head_committed = read_commit(head_id).files;

    std::string target_id = read_file(path.string());
    if (target_id.empty()) target_id = "null";
    std::unordered_map<std::string, std::string> target_committed;
    if (target_id != "null") target_committed = read_commit(target_id).files;

    auto working_opt = collect_working_files(rules);
    if (!working_opt) return 1;
    auto& working = *working_opt;

    // Hard error — no prompt.
    auto at_risk = files_at_risk(working, head_committed, target_committed);
    if (!at_risk.empty()) {
        std::cerr << color::red
                  << "error: checkout would destroy unsaved work:\n" << color::reset;
        for (auto& f : at_risk)
            std::cerr << "  " << color::yellow << f << color::reset << "\n";
        std::cerr << color::gray
                  << "Save your changes first: ion save \"...\"\n"
                  << color::reset;
        return 1;
    }

    if (!restore_commit(target_id, rules)) {
        std::cerr << color::red
                  << "error: checkout failed — repository may be in a partial state\n"
                  << color::reset;
        return 1;
    }
    if (!write_file(ION_HEAD, name)) return 1;

    std::cout << color::green << "Switched to branch " << color::bold << name
              << color::reset << "\n";
    if (target_id == "null")
        std::cout << color::gray << "  (empty branch — no files restored)\n" << color::reset;
    else
        std::cout << color::gray << "  at commit " << target_id << color::reset << "\n";
    return 0;
}

// ============================================================
// CMD: HISTORY / LOG
// ============================================================

int cmd_history(bool oneline = false) {
    if (!is_valid_repo()) return repo_error();

    std::string branch  = get_current_branch();
    std::string head_id = get_branch_commit(branch);

    if (!oneline)
        std::cout << color::bold << "History  " << color::cyan << branch
                  << color::reset << "\n\n";

    if (head_id == "null") {
        std::cout << color::gray << "No commits yet.\n" << color::reset;
        return 0;
    }

    int count = 0;
    std::string cid = head_id;

    while (cid != "null" && !cid.empty()) {
        Commit c = read_commit(cid);
        if (c.id.empty()) {
            std::cerr << color::red << "error: broken commit chain at " << cid
                      << color::reset << "\n";
            return 1;
        }

        if (oneline) {
            std::cout << color::yellow << std::left << std::setw(4) << c.id << color::reset
                      << "  " << color::gray << format_timestamp(c.timestamp) << color::reset
                      << "  " << c.message;
            if (cid == head_id) std::cout << color::cyan << "  *" << color::reset;
            std::cout << "\n";
        } else {
            print_commit_header(c, cid == head_id);
            std::cout << color::gray << "  files:   " << c.files.size() << " tracked\n"
                      << color::reset << "\n";
        }

        cid = c.parent;
        ++count;
    }

    if (!oneline)
        std::cout << color::gray << count << " commit(s) on branch " << branch << "\n"
                  << color::reset;
    return 0;
}

// ============================================================
// CMD: SHOW <commit>
// ============================================================

int cmd_show(const std::string& id, bool with_diff = false) {
    if (!is_valid_repo()) return repo_error();
    if (!is_safe_commit_id(id)) {
        std::cerr << color::red << "error: invalid commit id '" << id
                  << "' — commit ids must be numeric\n" << color::reset;
        return 1;
    }

    if (!fs::exists(ION_COMMITS + "/" + id)) {
        std::cerr << color::red << "error: commit " << id << " not found\n" << color::reset;
        return 1;
    }

    Commit c            = read_commit(id);
    std::string branch  = get_current_branch();
    std::string head_id = get_branch_commit(branch);

    print_separator();
    print_commit_header(c, id == head_id);
    std::cout << color::gray << "  files:   " << c.files.size() << " tracked\n" << color::reset;
    print_separator();

    std::unordered_map<std::string, std::string> parent_files;
    if (c.parent != "null") parent_files = read_commit(c.parent).files;

    std::vector<std::string> mod, add, del;
    for (auto& [f, h] : c.files) {
        auto it = parent_files.find(f);
        if (it == parent_files.end()) add.push_back(f);
        else if (it->second != h)     mod.push_back(f);
    }
    for (auto& [f, _] : parent_files)
        if (c.files.find(f) == c.files.end()) del.push_back(f);

    std::sort(mod.begin(), mod.end());
    std::sort(add.begin(), add.end());
    std::sort(del.begin(), del.end());

    if (add.empty() && mod.empty() && del.empty()) {
        std::cout << color::gray << "(no changes from parent)\n" << color::reset;
    } else {
        for (auto& f : mod) std::cout << "  " << color::yellow << "~ " << f << color::reset << "\n";
        for (auto& f : add) std::cout << "  " << color::green  << "+ " << f << color::reset << "\n";
        for (auto& f : del) std::cout << "  " << color::red    << "- " << f << color::reset << "\n";
    }

    if (with_diff && !(add.empty() && mod.empty() && del.empty())) {
        std::cout << "\n";
        print_separator();
        for (auto& f : mod) {
            std::string old_hash = parent_files.at(f);
            std::string new_hash = c.files.at(f);
            auto old_lines = read_lines(ION_OBJECTS + "/" + old_hash);
            auto new_lines = read_lines(ION_OBJECTS + "/" + new_hash);
            auto diff      = compute_diff(old_lines, new_lines);
            bool has_ch    = false;
            for (auto& d : diff) if (d.op != ' ') { has_ch = true; break; }
            if (!has_ch) continue;

            std::cout << color::bold << color::cyan << "diff " << f << color::reset << "\n";
            for (auto& d : diff) {
                if      (d.op == '+') std::cout << color::green << "+ " << d.text << color::reset << "\n";
                else if (d.op == '-') std::cout << color::red   << "- " << d.text << color::reset << "\n";
            }
            std::cout << "\n";
        }
        for (auto& f : add) {
            std::cout << color::bold << color::green << "+++ " << f
                      << color::gray << " (new file)" << color::reset << "\n";
            for (auto& l : read_lines(ION_OBJECTS + "/" + c.files.at(f)))
                std::cout << color::green << "+ " << l << color::reset << "\n";
            std::cout << "\n";
        }
        for (auto& f : del) {
            std::cout << color::bold << color::red << "--- " << f
                      << color::gray << " (deleted)" << color::reset << "\n\n";
        }
    }

    print_separator();
    return 0;
}

// ============================================================
// CMD: RESTORE (JUMP TO COMMIT)
//
// v0.4 change: the interactive "Continue? [y/N]" prompt is removed.
// The pre-flight check aborts with a hard error if any unsaved work
// would be destroyed.  No data is ever modified before the check.
// ============================================================

int cmd_restore(const std::string& id) {
    if (!is_valid_repo()) return repo_error();
    if (!is_safe_commit_id(id)) {
        std::cerr << color::red << "error: invalid commit id '" << id
                  << "' — commit ids must be numeric\n" << color::reset;
        return 1;
    }

    if (!fs::exists(ION_COMMITS + "/" + id)) {
        std::cerr << color::red << "error: commit " << id << " not found\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();

    // Build the three states needed for the pre-flight check.
    std::string branch  = get_current_branch();
    std::string head_id = get_branch_commit(branch);

    std::unordered_map<std::string, std::string> head_committed;
    if (head_id != "null") head_committed = read_commit(head_id).files;

    std::unordered_map<std::string, std::string> target_committed = read_commit(id).files;

    auto working_opt = collect_working_files(rules);
    if (!working_opt) return 1;
    auto& working = *working_opt;

    // Abort before touching anything if work would be lost — no prompt.
    auto at_risk = files_at_risk(working, head_committed, target_committed);
    if (!at_risk.empty()) {
        std::cerr << color::red << "error: restore would destroy unsaved work:\n"
                  << color::reset;
        for (auto& f : at_risk)
            std::cerr << "  " << color::yellow << f << color::reset << "\n";
        std::cerr << color::gray
                  << "Save your changes first: ion save \"...\"\n"
                  << color::reset;
        return 1;
    }

    if (!restore_commit(id, rules)) {
        std::cerr << color::red << "error: restore failed\n" << color::reset;
        return 1;
    }

    std::cout << color::green << "Restored commit " << id << "\n" << color::reset;
    std::cout << color::gray
              << "  HEAD remains on branch " << branch << " at commit " << head_id << ".\n"
              << "  Tip: run 'ion save \"...\"' to create a new commit from this state.\n"
              << color::reset;
    return 0;
}

// ============================================================
// CMD: VERIFY  (read-only integrity check)
// ============================================================

int cmd_verify() {
    if (!is_valid_repo()) return repo_error();

    bool ok = true;
    int  defect_count = 0;

    auto defect = [&](const std::string& msg) {
        std::cerr << color::red << "defect: " << msg << color::reset << "\n";
        ok = false;
        ++defect_count;
    };

    // 1. Verify HEAD points to a real branch.
    std::string branch = get_current_branch();
    if (branch.empty()) {
        defect("HEAD is empty");
    } else if (!fs::exists(ION_BRANCHES + "/" + branch)) {
        defect("HEAD points to missing branch '" + branch + "'");
    }

    // 2. Enumerate all branches.
    std::vector<std::string> branches;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(ION_BRANCHES, ec))
        branches.push_back(e.path().filename().string());
    if (ec) { defect("cannot read branches directory"); return 1; }
    std::sort(branches.begin(), branches.end());

    // 3. Walk every commit chain reachable from any branch.
    std::unordered_set<std::string> visited;
    int commit_count = 0;

    for (auto& br : branches) {
        std::string cid = get_branch_commit(br);
        int depth = 0;

        while (cid != "null" && !cid.empty()) {
            if (visited.count(cid)) break; // already validated — skip shared history
            visited.insert(cid);
            ++commit_count;

            if (!fs::exists(ION_COMMITS + "/" + cid)) {
                defect("branch '" + br + "' references missing commit " + cid);
                break;
            }

            Commit c = read_commit(cid);
            if (c.id.empty()) {
                defect("commit " + cid + " is malformed or unreadable");
                break;
            }

            // 4. Verify every file object referenced by this commit.
            for (auto& [f, h] : c.files) {
                if (!fs::exists(ION_OBJECTS + "/" + h))
                    defect("commit " + cid + ": missing object " + h + " ('" + f + "')");
            }

            cid = c.parent;
            if (++depth > 1000000) {
                defect("commit chain cycle or runaway detected near commit " + cid);
                break;
            }
        }
    }

    // Summary.
    std::cout << "\n";
    print_separator();
    if (ok) {
        std::cout << color::green << color::bold << "Repository OK\n" << color::reset;
    } else {
        std::cout << color::red << color::bold
                  << "Repository has " << defect_count << " defect(s)\n"
                  << color::reset;
    }
    std::cout << color::gray
              << "  branches: " << branches.size() << "\n"
              << "  commits:  " << commit_count    << "\n"
              << color::reset;
    print_separator();

    return ok ? 0 : 1;
}

// ============================================================
// HELP
// ============================================================

void print_help() {
    std::cout << color::bold << "ion  —  a local-first version control system\n\n" << color::reset;

    auto row = [](const std::string& cmd, const std::string& desc) {
        std::cout << "  " << color::cyan << std::left << std::setw(40) << cmd
                  << color::reset << desc << "\n";
    };

    std::cout << color::bold << "Repository:\n" << color::reset;
    row("ion init",                           "Initialize a new repository");
    row("ion verify",                         "Check repository integrity (read-only)");

    std::cout << color::bold << "\nSnapshots:\n" << color::reset;
    row("ion save <message>",                 "Save a snapshot of the working directory");
    row("ion restore <commit>",               "Restore working directory to a commit");

    std::cout << color::bold << "\nInspection:\n" << color::reset;
    row("ion status",                         "Show working directory status");
    row("ion status --ignored",               "Also list ignored files");
    row("ion diff",                           "Show line-level changes vs last commit");
    row("ion diff <file>",                    "Diff a specific file");
    row("ion history",                        "Full commit history for current branch");
    row("ion log --oneline",                  "Compact one-line history");
    row("ion show <commit>",                  "Show commit details and file list");
    row("ion show <commit> --diff",           "Show commit details with inline diff");

    std::cout << color::bold << "\nBranches:\n" << color::reset;
    row("ion branch <name>",                  "Create a new branch");
    row("ion branches",                       "List all branches");
    row("ion checkout <branch>",              "Switch to a branch");
    row("ion switch <branch>",                "Alias for checkout");
    row("ion branch-delete <name>",           "Delete a branch (commits are kept)");

    std::cout << "\n" << color::gray
              << "Ignore rules: create a .ionignore file in the project root\n"
              << "  Patterns:  build/   *.log   secret.txt\n"
              << color::reset;
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[]) {
    if (argc < 2) { print_help(); return 0; }

    std::string cmd = argv[1];

    // Split remaining argv into positional args and --flag tokens.
    std::vector<std::string> args;
    std::unordered_set<std::string> flags;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) flags.insert(a);
        else args.push_back(a);
    }

    auto arg      = [&](int idx) -> std::string {
        return (static_cast<size_t>(idx) < args.size()) ? args[static_cast<size_t>(idx)] : "";
    };
    auto has_flag = [&](const std::string& f) { return flags.count(f) > 0; };

    if (cmd == "init") {
        return cmd_init();

    } else if (cmd == "save") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion save <message>\n" << color::reset;
            return 1;
        }
        return cmd_save(arg(0));

    } else if (cmd == "status") {
        return cmd_status(has_flag("--ignored"));

    } else if (cmd == "diff") {
        return cmd_diff(arg(0));

    } else if (cmd == "history" || cmd == "log") {
        return cmd_history(has_flag("--oneline"));

    } else if (cmd == "show") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion show <commit> [--diff]\n" << color::reset;
            return 1;
        }
        return cmd_show(arg(0), has_flag("--diff"));

    } else if (cmd == "branch") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion branch <name>\n" << color::reset;
            return 1;
        }
        return cmd_branch(arg(0));

    } else if (cmd == "branch-delete") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion branch-delete <name>\n" << color::reset;
            return 1;
        }
        return cmd_branch_delete(arg(0));

    } else if (cmd == "branches") {
        return cmd_branches();

    } else if (cmd == "checkout" || cmd == "switch") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion " << cmd << " <branch>\n" << color::reset;
            return 1;
        }
        return cmd_checkout(arg(0));

    } else if (cmd == "restore") {
        if (args.empty()) {
            std::cerr << color::red << "error: usage: ion restore <commit>\n" << color::reset;
            return 1;
        }
        return cmd_restore(arg(0));

    } else if (cmd == "verify") {
        return cmd_verify();

    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;

    } else {
        std::cerr << color::red << "error: unknown command '" << cmd << "'\n" << color::reset;
        std::cerr << color::gray << "Run 'ion help' for a list of commands.\n" << color::reset;
        return 1;
    }
}