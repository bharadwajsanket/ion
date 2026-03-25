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
#include <iomanip>
#include <functional>
#include <set>
#include <map>
#include <optional>

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

// Read first line of a file. Returns "" on failure.
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
    // Only create parent directories if there actually is a parent to create.
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

// Prints an error and returns 1. Used for early-exit in commands.
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
// HASHING
// ============================================================

// Content-hash a file using std::hash. Returns 16-char hex string.
std::string hash_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "0000000000000000";
    std::ostringstream buf;
    buf << in.rdbuf();
    size_t h = std::hash<std::string>{}(buf.str());
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
// .IONIGNORE
// ============================================================

struct IgnoreRules {
    std::vector<std::string> extensions;   // e.g. ".log"
    std::vector<std::string> dir_prefixes; // e.g. "build/"
    std::vector<std::string> filenames;    // exact name or relative path
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
            rules.dir_prefixes.push_back(line); // "build/"
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
// COMMIT STRUCT
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
    std::string line;
    bool in_files = false;

    while (std::getline(in, line)) {
        if (!in_files) {
            if      (line.rfind("id: ", 0) == 0)        c.id        = line.substr(4);
            else if (line.rfind("parent: ", 0) == 0)    c.parent    = line.substr(8);
            else if (line.rfind("message: ", 0) == 0)   c.message   = line.substr(9);
            else if (line.rfind("timestamp: ", 0) == 0) c.timestamp = line.substr(11);
            else if (line == "files:")                   in_files    = true;
        } else {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string f, h;
            iss >> f >> h;
            if (!f.empty() && !h.empty()) c.files[f] = h;
        }
    }
    return c;
}

bool write_commit(const Commit& c) {
    std::error_code ec;
    fs::create_directories(ION_COMMITS, ec);
    std::string path = ION_COMMITS + "/" + c.id;
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::cerr << color::red << "error: cannot write commit " << c.id << color::reset << "\n";
        return false;
    }
    out << "id: "        << c.id        << "\n"
        << "parent: "    << c.parent    << "\n"
        << "message: "   << c.message   << "\n"
        << "timestamp: " << c.timestamp << "\n"
        << "files:\n";
    // Sort for deterministic output.
    std::vector<std::pair<std::string, std::string>> sorted(c.files.begin(), c.files.end());
    std::sort(sorted.begin(), sorted.end());
    for (auto& [f, h] : sorted) out << f << " " << h << "\n";
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

std::unordered_map<std::string, std::string>
collect_working_files(const IgnoreRules& rules) {
    std::unordered_map<std::string, std::string> result;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(
             ".", fs::directory_options::skip_permission_denied, ec)) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (should_ignore(rel, rules)) continue;
        result[rel] = hash_file(entry.path());
    }
    return result;
}

// Collect only ignored files (for status --ignored display).
std::vector<std::string>
collect_ignored_files(const IgnoreRules& rules) {
    std::vector<std::string> result;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(
             ".", fs::directory_options::skip_permission_denied, ec)) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (rel.rfind(".ion/", 0) == 0 || rel == ".ion") continue;
        if (should_ignore(rel, rules)) result.push_back(rel);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ============================================================
// RESTORE WORKING DIRECTORY
// ============================================================

bool restore_commit(const std::string& id, const IgnoreRules& rules) {
    // Remove all non-ignored, non-.ion files.
    std::vector<fs::path> to_remove;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(
             ".", fs::directory_options::skip_permission_denied, ec)) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = norm(fs::relative(entry.path(), ".").string());
        if (!should_ignore(rel, rules)) to_remove.push_back(entry.path());
    }
    for (auto& p : to_remove) {
        fs::remove(p, ec);
        if (ec) {
            std::cerr << color::red << "error: cannot remove " << p
                      << ": " << ec.message() << color::reset << "\n";
            return false;
        }
    }

    if (id == "null" || id.empty()) return true;

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
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        return buf;
    } catch (...) { return ts_str; }
}

// ============================================================
// DIFF ENGINE
// ============================================================

struct DiffLine { char op; std::string text; }; // op: ' ', '+', '-'

// LCS-based line diff.
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

    // Group changes into hunks with context.
    std::vector<size_t> changed;
    for (size_t k = 0; k < diff.size(); ++k)
        if (diff[k].op != ' ') changed.push_back(k);

    std::vector<std::pair<size_t, size_t>> ranges;
    for (size_t ci : changed) {
        size_t lo = (ci >= (size_t)context) ? ci - context : 0;
        size_t hi = std::min(ci + context + 1, diff.size());
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
    std::error_code ec;
    fs::create_directories(ION_COMMITS,  ec);
    fs::create_directories(ION_OBJECTS,  ec);
    fs::create_directories(ION_BRANCHES, ec);
    if (ec) {
        std::cerr << color::red << "error: failed to create repository structure: "
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

    auto working      = collect_working_files(rules);
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

    auto working = collect_working_files(rules);

    std::set<std::string> all_files;
    for (auto& [f, _] : working)   all_files.insert(f);
    for (auto& [f, _] : committed) all_files.insert(f);

    bool any_diff = false;

    for (auto& file : all_files) {
        if (!target_file.empty() && file != target_file) continue;

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
// ============================================================

int cmd_save(const std::string& message, bool confirm) {
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

    auto working = collect_working_files(rules);
    auto s       = compute_status(committed, working, {});

    if (!has_changes(s)) {
        std::cout << color::yellow
                  << "Nothing to save — working directory matches last commit.\n"
                  << color::reset;
        return 0;
    }

    std::cout << color::bold << "Changes to save:\n" << color::reset;
    print_change_summary(s);
    std::cout << "\n";

    if (confirm) {
        std::cout << "Proceed with save? [y/N] ";
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "y" && resp != "Y") {
            std::cout << color::gray << "Save aborted.\n" << color::reset;
            return 0;
        }
    }

    // Store content objects.
    std::error_code ec;
    fs::create_directories(ION_OBJECTS, ec);
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
    if (name.empty()) {
        std::cerr << color::red << "error: branch name cannot be empty\n" << color::reset;
        return 1;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        std::cerr << color::red << "error: branch name may not contain path separators\n"
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
// CMD: BRANCHES (LIST)
// ============================================================

int cmd_branches() {
    if (!is_valid_repo()) return repo_error();

    std::string current = get_current_branch();
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(ION_BRANCHES, ec))
        names.push_back(entry.path().filename().string());
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
// CMD: CHECKOUT
// ============================================================

int cmd_checkout(const std::string& name) {
    if (!is_valid_repo()) return repo_error();

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

    // Detect unsaved changes.
    std::string head_commit = get_branch_commit(current);
    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") committed = read_commit(head_commit).files;
    auto working = collect_working_files(rules);
    auto s       = compute_status(committed, working, {});

    if (has_changes(s)) {
        std::cout << color::yellow
                  << "warning: " << change_stats(s) << " on branch '" << current << "'.\n"
                  << "  These will be lost if you switch now.\n"
                  << "  Save first with: ion save \"...\"\n\n"
                  << "  Continue anyway? [y/N] " << color::reset;
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "y" && resp != "Y") {
            std::cout << color::gray << "Checkout aborted.\n" << color::reset;
            return 0;
        }
    }

    std::string target_commit = read_file(path.string());
    if (target_commit.empty()) target_commit = "null";

    if (!restore_commit(target_commit, rules)) {
        std::cerr << color::red
                  << "error: checkout failed — repository may be in a partial state\n"
                  << color::reset;
        return 1;
    }
    if (!write_file(ION_HEAD, name)) return 1;

    std::cout << color::green << "Switched to branch " << color::bold << name << color::reset << "\n";
    if (target_commit == "null")
        std::cout << color::gray << "  (empty branch — no files restored)\n" << color::reset;
    else
        std::cout << color::gray << "  at commit " << target_commit << color::reset << "\n";
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

    if (!fs::exists(ION_COMMITS + "/" + id)) {
        std::cerr << color::red << "error: commit " << id << " not found\n" << color::reset;
        return 1;
    }

    Commit c       = read_commit(id);
    std::string branch  = get_current_branch();
    std::string head_id = get_branch_commit(branch);

    print_separator();
    print_commit_header(c, id == head_id);
    std::cout << color::gray << "  files:   " << c.files.size() << " tracked\n" << color::reset;
    print_separator();

    // Compute what changed vs parent.
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

    // Optional inline diff from stored objects.
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
// ============================================================

int cmd_restore(const std::string& id) {
    if (!is_valid_repo()) return repo_error();

    if (!fs::exists(ION_COMMITS + "/" + id)) {
        std::cerr << color::red << "error: commit " << id << " not found\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();

    std::cout << color::yellow
              << "warning: this will overwrite your working directory with commit " << id << ".\n"
              << "  Your HEAD pointer will not change.\n"
              << "  Continue? [y/N] " << color::reset;
    std::string resp;
    std::getline(std::cin, resp);
    if (resp != "y" && resp != "Y") {
        std::cout << color::gray << "Restore aborted.\n" << color::reset;
        return 0;
    }

    if (!restore_commit(id, rules)) {
        std::cerr << color::red << "error: restore failed\n" << color::reset;
        return 1;
    }

    std::cout << color::green << "Restored commit " << id << "\n" << color::reset;
    std::cout << color::gray
              << "  Tip: run 'ion save \"...\"' to create a new commit from this state.\n"
              << color::reset;
    return 0;
}

// ============================================================
// HELP
// ============================================================

void print_help() {
    std::cout << color::bold << "ion  —  a local-first version control system\n\n" << color::reset;

    auto row = [](const std::string& cmd, const std::string& desc) {
        std::cout << "  " << color::cyan << std::left << std::setw(38) << cmd
                  << color::reset << desc << "\n";
    };

    std::cout << color::bold << "Repository:\n" << color::reset;
    row("ion init",                         "Initialize a new repository");

    std::cout << color::bold << "\nSnapshots:\n" << color::reset;
    row("ion save <message>",               "Save a snapshot");
    row("ion save <message> --confirm",     "Save with confirmation prompt");
    row("ion restore <commit>",             "Restore working directory to a commit");

    std::cout << color::bold << "\nInspection:\n" << color::reset;
    row("ion status",                       "Show working directory status");
    row("ion status --ignored",             "Also list ignored files");
    row("ion diff",                         "Show line-level changes vs last commit");
    row("ion diff <file>",                  "Diff a specific file");
    row("ion history",                      "Full commit history for current branch");
    row("ion log --oneline",                "Compact one-line history");
    row("ion show <commit>",                "Show commit details and file list");
    row("ion show <commit> --diff",         "Show commit details with inline diff");

    std::cout << color::bold << "\nBranches:\n" << color::reset;
    row("ion branch <name>",                "Create a new branch");
    row("ion branches",                     "List all branches");
    row("ion checkout <branch>",            "Switch to a branch");

    std::cout << "\n" << color::gray
              << "Ignore rules: create a .ionignore file\n"
              << "  Patterns:  build/   *.log   secret.txt\n"
              << color::reset;
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[]) {
    if (argc < 2) { print_help(); return 0; }

    std::string cmd = argv[1];

    // Parse positional args and --flags separately.
    std::vector<std::string> args;
    std::unordered_set<std::string> flags;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) flags.insert(a);
        else args.push_back(a);
    }

    auto arg = [&](int idx) -> std::string {
        return (idx < (int)args.size()) ? args[idx] : "";
    };
    auto has_flag = [&](const std::string& f) { return flags.count(f) > 0; };

    if (cmd == "init") {
        return cmd_init();

    } else if (cmd == "save") {
        if (args.empty()) {
            std::cerr << color::red
                      << "error: usage: ion save <message> [--confirm]\n"
                      << color::reset;
            return 1;
        }
        return cmd_save(arg(0), has_flag("--confirm"));

    } else if (cmd == "status") {
        return cmd_status(has_flag("--ignored"));

    } else if (cmd == "diff") {
        return cmd_diff(arg(0));

    } else if (cmd == "history" || cmd == "log") {
        return cmd_history(has_flag("--oneline"));

    } else if (cmd == "show") {
        if (args.empty()) {
            std::cerr << color::red
                      << "error: usage: ion show <commit> [--diff]\n"
                      << color::reset;
            return 1;
        }
        return cmd_show(arg(0), has_flag("--diff"));

    } else if (cmd == "branch") {
        if (args.empty()) {
            std::cerr << color::red
                      << "error: usage: ion branch <name>\n"
                      << color::reset;
            return 1;
        }
        return cmd_branch(arg(0));

    } else if (cmd == "branches") {
        return cmd_branches();

    } else if (cmd == "checkout") {
        if (args.empty()) {
            std::cerr << color::red
                      << "error: usage: ion checkout <branch>\n"
                      << color::reset;
            return 1;
        }
        return cmd_checkout(arg(0));

    } else if (cmd == "restore") {
        if (args.empty()) {
            std::cerr << color::red
                      << "error: usage: ion restore <commit>\n"
                      << color::reset;
            return 1;
        }
        return cmd_restore(arg(0));

    } else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;

    } else {
        std::cerr << color::red << "error: unknown command '" << cmd << "'\n" << color::reset;
        std::cerr << color::gray << "Run 'ion help' for a list of commands.\n" << color::reset;
        return 1;
    }
}