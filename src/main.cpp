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

namespace fs = std::filesystem;

// ============================================================
// CONSTANTS
// ============================================================

static const std::string ION_DIR        = ".ion";
static const std::string ION_HEAD       = ".ion/HEAD";
static const std::string ION_BRANCHES   = ".ion/branches";
static const std::string ION_COMMITS    = ".ion/commits";
static const std::string ION_OBJECTS    = ".ion/objects/files";
static const std::string ION_IGNORE     = ".ionignore";

// ============================================================
// TERMINAL COLOR HELPERS
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
}

// ============================================================
// FILE HELPERS
// ============================================================

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::string value;
    std::getline(in, value);
    return value;
}

std::string read_file_full(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& value) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        std::cerr << color::red << "error: cannot write to " << path << color::reset << "\n";
        return;
    }
    out << value;
}

std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in) return lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

// ============================================================
// REPO CHECK
// ============================================================

bool is_valid_repo() {
    return fs::exists(ION_HEAD)     &&
           fs::exists(ION_BRANCHES) &&
           fs::exists(ION_COMMITS)  &&
           fs::exists(ION_OBJECTS);
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
    return read_file(path);
}

void set_branch_commit(const std::string& branch, const std::string& commit) {
    write_file(ION_BRANCHES + "/" + branch, commit);
}

// ============================================================
// HASH
// ============================================================

std::string hash_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "0";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    size_t h = std::hash<std::string>{}(buffer.str());
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << h;
    return hex.str();
}

std::string hash_string(const std::string& s) {
    size_t h = std::hash<std::string>{}(s);
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << h;
    return hex.str();
}

// ============================================================
// .IONIGNORE
// ============================================================

struct IgnoreRules {
    std::vector<std::string> exact;    // e.g. build/
    std::vector<std::string> prefix;   // e.g. build/ treated as prefix
    std::vector<std::string> ext;      // e.g. *.log -> .log
    std::vector<std::string> filename; // exact filenames
};

IgnoreRules load_ignore_rules() {
    IgnoreRules rules;
    if (!fs::exists(ION_IGNORE)) return rules;
    auto lines = read_lines(ION_IGNORE);
    for (auto& raw : lines) {
        std::string line = raw;
        // trim
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
        if (line.empty() || line[0] == '#') continue;

        if (line.size() > 2 && line[0] == '*' && line[1] == '.') {
            // extension pattern like *.log
            rules.ext.push_back(line.substr(1)); // store as .log
        } else if (!line.empty() && line.back() == '/') {
            // directory pattern like build/
            rules.prefix.push_back(line);
        } else {
            rules.filename.push_back(line);
        }
    }
    return rules;
}

bool should_ignore(const std::string& rel, const IgnoreRules& rules) {
    // Always ignore .ion directory
    if (rel == ".ion" || rel.rfind(".ion/", 0) == 0 || rel.rfind(".ion\\", 0) == 0)
        return true;
    // Always ignore .ionignore itself (don't track it in VCS)
    if (rel == ION_IGNORE) return false; // actually track it

    for (auto& ext : rules.ext) {
        if (rel.size() >= ext.size() && rel.substr(rel.size() - ext.size()) == ext)
            return true;
    }
    for (auto& prefix : rules.prefix) {
        // prefix is like "build/"
        if (rel.rfind(prefix, 0) == 0) return true;
        // handle path separator on Windows
        std::string prefix_bs = prefix;
        std::replace(prefix_bs.begin(), prefix_bs.end(), '/', '\\');
        if (rel.rfind(prefix_bs, 0) == 0) return true;
        // also match "build/..." where first component is build
        std::string dir = prefix.substr(0, prefix.size() - 1); // strip trailing /
        if (rel == dir) return true;
        if (rel.rfind(dir + "/", 0) == 0) return true;
        if (rel.rfind(dir + "\\", 0) == 0) return true;
    }
    for (auto& fname : rules.filename) {
        if (rel == fname) return true;
        // match basename
        fs::path p(rel);
        if (p.filename().string() == fname) return true;
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
    std::unordered_map<std::string, std::string> files; // rel -> hash
};

Commit read_commit(const std::string& id) {
    Commit c;
    c.id = id;
    if (id == "null") return c;

    std::string path = ION_COMMITS + "/" + id;
    if (!fs::exists(path)) return c;

    std::ifstream in(path);
    std::string line;
    bool in_files = false;

    while (std::getline(in, line)) {
        if (line.rfind("id: ", 0) == 0)        c.id        = line.substr(4);
        else if (line.rfind("parent: ", 0) == 0)    c.parent    = line.substr(8);
        else if (line.rfind("message: ", 0) == 0)   c.message   = line.substr(9);
        else if (line.rfind("timestamp: ", 0) == 0) c.timestamp = line.substr(11);
        else if (line == "files:") { in_files = true; continue; }
        else if (in_files && !line.empty()) {
            std::istringstream iss(line);
            std::string f, h;
            iss >> f >> h;
            if (!f.empty() && !h.empty())
                c.files[f] = h;
        }
    }
    return c;
}

bool write_commit(const Commit& c) {
    std::string path = ION_COMMITS + "/" + c.id;
    fs::create_directories(ION_COMMITS);
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "id: "        << c.id        << "\n";
    out << "parent: "    << c.parent    << "\n";
    out << "message: "   << c.message   << "\n";
    out << "timestamp: " << c.timestamp << "\n";
    out << "files:\n";
    for (auto& [f, h] : c.files)
        out << f << " " << h << "\n";
    return true;
}

// ============================================================
// NEXT COMMIT ID
// ============================================================

std::string next_commit_id() {
    // Find the highest numeric commit and increment
    int max_id = 0;
    if (fs::exists(ION_COMMITS)) {
        for (auto& entry : fs::directory_iterator(ION_COMMITS)) {
            std::string name = entry.path().filename().string();
            try {
                int n = std::stoi(name);
                if (n > max_id) max_id = n;
            } catch (...) {}
        }
    }
    return std::to_string(max_id + 1);
}

// ============================================================
// COLLECT WORKING DIRECTORY FILES
// ============================================================

std::unordered_map<std::string, std::string> collect_working_files(const IgnoreRules& rules) {
    std::unordered_map<std::string, std::string> result;
    if (!fs::exists(".")) return result;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(".", fs::directory_options::skip_permission_denied, ec)) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = fs::relative(entry.path(), ".").string();
        // Normalize separators
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (should_ignore(rel, rules)) continue;
        result[rel] = hash_file(entry.path());
    }
    return result;
}

// ============================================================
// RESTORE
// ============================================================

void restore_commit(const std::string& id, const IgnoreRules& rules) {
    // Remove all tracked files in working dir (skip .ion and ignored)
    std::vector<fs::path> to_remove;
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(".", fs::directory_options::skip_permission_denied, ec)) {
        if (!fs::is_regular_file(entry.path())) continue;
        std::string rel = fs::relative(entry.path(), ".").string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (!should_ignore(rel, rules)) {
            to_remove.push_back(entry.path());
        }
    }
    for (auto& p : to_remove) {
        fs::remove(p, ec);
    }

    if (id == "null") return;

    Commit c = read_commit(id);
    for (auto& [file, hash] : c.files) {
        fs::path src = ION_OBJECTS + "/" + hash;
        fs::path dst = file;
        if (!fs::exists(src)) {
            std::cerr << color::red << "error: object missing for " << file << color::reset << "\n";
            continue;
        }
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << color::red << "error: failed to restore " << file << ": " << ec.message() << color::reset << "\n";
        }
    }
}

// ============================================================
// DIFF HELPERS
// ============================================================

struct DiffResult {
    std::vector<std::string> added;
    std::vector<std::string> removed;
};

DiffResult diff_lines(const std::vector<std::string>& old_lines,
                      const std::vector<std::string>& new_lines) {
    DiffResult result;
    // Simple O(n+m) LCS-based line diff
    size_t n = old_lines.size(), m = new_lines.size();
    // Build LCS table
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j)
            if (old_lines[i-1] == new_lines[j-1])
                dp[i][j] = dp[i-1][j-1] + 1;
            else
                dp[i][j] = std::max(dp[i-1][j], dp[i][j-1]);

    // Traceback
    std::vector<std::pair<char, std::string>> diff;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && old_lines[i-1] == new_lines[j-1]) {
            diff.push_back({' ', old_lines[i-1]});
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            diff.push_back({'+', new_lines[j-1]});
            --j;
        } else {
            diff.push_back({'-', old_lines[i-1]});
            --i;
        }
    }
    std::reverse(diff.begin(), diff.end());

    for (auto& [type, line] : diff) {
        if (type == '+') result.added.push_back(line);
        else if (type == '-') result.removed.push_back(line);
    }
    return result;
}

void print_file_diff(const std::string& filename,
                     const std::string& old_hash,
                     const std::string& new_hash) {
    std::vector<std::string> old_lines, new_lines;
    if (old_hash != "null" && fs::exists(ION_OBJECTS + "/" + old_hash))
        old_lines = read_lines(ION_OBJECTS + "/" + old_hash);
    if (new_hash != "null" && fs::exists(filename))
        new_lines = read_lines(filename);

    size_t n = old_lines.size(), m = new_lines.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j)
            if (old_lines[i-1] == new_lines[j-1])
                dp[i][j] = dp[i-1][j-1] + 1;
            else
                dp[i][j] = std::max(dp[i-1][j], dp[i][j-1]);

    std::vector<std::pair<char, std::string>> diff;
    size_t i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && old_lines[i-1] == new_lines[j-1]) {
            diff.push_back({' ', old_lines[i-1]});
            --i; --j;
        } else if (j > 0 && (i == 0 || dp[i][j-1] >= dp[i-1][j])) {
            diff.push_back({'+', new_lines[j-1]});
            --j;
        } else {
            diff.push_back({'-', old_lines[i-1]});
            --i;
        }
    }
    std::reverse(diff.begin(), diff.end());

    bool has_changes = false;
    for (auto& [t, _] : diff) if (t != ' ') { has_changes = true; break; }
    if (!has_changes) return;

    std::cout << color::bold << color::cyan << "--- " << filename << color::reset << "\n";
    for (auto& [type, line] : diff) {
        if (type == '+')      std::cout << color::green << "+ " << line << color::reset << "\n";
        else if (type == '-') std::cout << color::red   << "- " << line << color::reset << "\n";
        // skip context lines for brevity
    }
}

// ============================================================
// FORMAT TIMESTAMP
// ============================================================

std::string format_timestamp(const std::string& ts_str) {
    if (ts_str.empty()) return "unknown";
    try {
        time_t ts = (time_t)std::stoll(ts_str);
        std::tm* t = std::localtime(&ts);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        return std::string(buf);
    } catch (...) {
        return ts_str;
    }
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
    fs::create_directory(ION_DIR, ec);
    fs::create_directories(ION_COMMITS, ec);
    fs::create_directories(ION_OBJECTS, ec);
    fs::create_directories(ION_BRANCHES, ec);

    write_file(ION_HEAD, "main");
    write_file(ION_BRANCHES + "/main", "null");

    std::cout << color::green << "Initialized ion repository.\n" << color::reset;
    std::cout << color::gray  << "  branch: main\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: STATUS
// ============================================================

int cmd_status() {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();
    std::string branch = get_current_branch();
    std::string head_commit = get_branch_commit(branch);

    std::cout << color::bold << "On branch " << color::cyan << branch << color::reset;
    if (head_commit == "null")
        std::cout << color::gray << "  (no commits yet)" << color::reset;
    std::cout << "\n\n";

    // Get last commit file map
    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") {
        Commit c = read_commit(head_commit);
        committed = c.files;
    }

    // Get working directory files
    auto working = collect_working_files(rules);

    std::vector<std::string> modified, added, deleted;

    for (auto& [file, hash] : working) {
        auto it = committed.find(file);
        if (it == committed.end()) {
            added.push_back(file);
        } else if (it->second != hash) {
            modified.push_back(file);
        }
    }
    for (auto& [file, _] : committed) {
        if (working.find(file) == working.end()) {
            deleted.push_back(file);
        }
    }

    std::sort(modified.begin(), modified.end());
    std::sort(added.begin(), added.end());
    std::sort(deleted.begin(), deleted.end());

    bool clean = modified.empty() && added.empty() && deleted.empty();

    if (clean) {
        std::cout << color::green << "Nothing to save, working directory clean.\n" << color::reset;
        return 0;
    }

    if (!modified.empty()) {
        std::cout << color::bold << "Modified:\n" << color::reset;
        for (auto& f : modified)
            std::cout << "  " << color::yellow << "~ " << f << color::reset << "\n";
        std::cout << "\n";
    }
    if (!added.empty()) {
        std::cout << color::bold << "Untracked:\n" << color::reset;
        for (auto& f : added)
            std::cout << "  " << color::green << "+ " << f << color::reset << "\n";
        std::cout << "\n";
    }
    if (!deleted.empty()) {
        std::cout << color::bold << "Deleted:\n" << color::reset;
        for (auto& f : deleted)
            std::cout << "  " << color::red << "- " << f << color::reset << "\n";
        std::cout << "\n";
    }

    int total = (int)(modified.size() + added.size() + deleted.size());
    std::cout << color::gray << total << " file(s) changed.\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: DIFF
// ============================================================

int cmd_diff(const std::string& target_file = "") {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();
    std::string branch = get_current_branch();
    std::string head_commit = get_branch_commit(branch);

    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") {
        Commit c = read_commit(head_commit);
        committed = c.files;
    }

    auto working = collect_working_files(rules);

    // Collect all files to diff
    std::set<std::string> all_files;
    for (auto& [f, _] : working)   all_files.insert(f);
    for (auto& [f, _] : committed) all_files.insert(f);

    bool any_diff = false;

    for (auto& file : all_files) {
        if (!target_file.empty() && file != target_file) continue;

        std::string old_hash = "null";
        std::string new_hash = "null";

        auto ci = committed.find(file);
        auto wi = working.find(file);

        if (ci != committed.end()) old_hash = ci->second;
        if (wi != working.end())   new_hash = wi->second;

        if (old_hash == new_hash) continue; // unchanged

        any_diff = true;

        if (old_hash == "null") {
            std::cout << color::bold << color::green << "+++ " << file
                      << color::gray << " (new file)" << color::reset << "\n";
            auto lines = read_lines(file);
            for (auto& l : lines)
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

    if (!any_diff) {
        std::cout << color::gray << "No differences from last commit.\n" << color::reset;
    }

    return 0;
}

// ============================================================
// CMD: SAVE
// ============================================================

int cmd_save(const std::string& message, bool confirm) {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }
    if (message.empty()) {
        std::cerr << color::red << "error: save message cannot be empty\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();
    std::string branch = get_current_branch();
    std::string parent = get_branch_commit(branch);

    // Compare with last commit
    std::unordered_map<std::string, std::string> committed;
    if (parent != "null") {
        Commit prev = read_commit(parent);
        committed = prev.files;
    }

    auto working = collect_working_files(rules);

    // Detect changes
    std::vector<std::string> modified, added_files, deleted_files;
    for (auto& [f, h] : working) {
        auto it = committed.find(f);
        if (it == committed.end()) added_files.push_back(f);
        else if (it->second != h)  modified.push_back(f);
    }
    for (auto& [f, _] : committed) {
        if (working.find(f) == working.end()) deleted_files.push_back(f);
    }

    if (modified.empty() && added_files.empty() && deleted_files.empty()) {
        std::cout << color::yellow << "Nothing to save — working directory matches last commit.\n" << color::reset;
        return 0;
    }

    // Show summary
    std::cout << color::bold << "Changes to save:\n" << color::reset;
    for (auto& f : modified)      std::cout << "  " << color::yellow << "~ " << f << color::reset << "\n";
    for (auto& f : added_files)   std::cout << "  " << color::green  << "+ " << f << color::reset << "\n";
    for (auto& f : deleted_files) std::cout << "  " << color::red    << "- " << f << color::reset << "\n";
    std::cout << "\n";

    // Confirmation prompt
    if (confirm) {
        std::cout << "Proceed with save? [y/N] ";
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "y" && resp != "Y") {
            std::cout << color::gray << "Save aborted.\n" << color::reset;
            return 0;
        }
    }

    // Build commit
    Commit c;
    c.id      = next_commit_id();
    c.parent  = parent;
    c.message = message;
    c.timestamp = std::to_string(std::time(nullptr));
    c.files   = working;

    // Store objects
    std::error_code ec;
    fs::create_directories(ION_OBJECTS, ec);
    for (auto& [rel, hash] : working) {
        fs::path obj = ION_OBJECTS + "/" + hash;
        if (!fs::exists(obj)) {
            fs::copy_file(rel, obj, fs::copy_options::skip_existing, ec);
            if (ec) {
                std::cerr << color::red << "error: failed to store " << rel << ": "
                          << ec.message() << color::reset << "\n";
                return 1;
            }
        }
    }

    if (!write_commit(c)) {
        std::cerr << color::red << "error: failed to write commit\n" << color::reset;
        return 1;
    }

    set_branch_commit(branch, c.id);

    std::cout << color::green << color::bold << "Saved snapshot " << c.id
              << color::reset << color::gray << " on branch " << branch << "\n" << color::reset;
    std::cout << color::gray << "  " << (int)(modified.size() + added_files.size() + deleted_files.size())
              << " file(s) changed\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: BRANCH (CREATE)
// ============================================================

int cmd_branch(const std::string& name) {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }
    if (name.empty()) {
        std::cerr << color::red << "error: branch name cannot be empty\n" << color::reset;
        return 1;
    }

    fs::path path = ION_BRANCHES + "/" + name;
    if (fs::exists(path)) {
        std::cerr << color::red << "error: branch '" << name << "' already exists\n" << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    std::string commit  = get_branch_commit(current);
    write_file(path.string(), commit);

    std::cout << color::green << "Created branch " << color::bold << name << color::reset << "\n";
    std::cout << color::gray  << "  branched from " << current;
    if (commit != "null") std::cout << " at commit " << commit;
    std::cout << "\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: BRANCHES (LIST)
// ============================================================

int cmd_branches() {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    std::vector<std::string> names;

    for (auto& entry : fs::directory_iterator(ION_BRANCHES)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());

    for (auto& name : names) {
        std::string commit = get_branch_commit(name);
        if (name == current) {
            std::cout << color::green << color::bold << "* " << name << color::reset;
        } else {
            std::cout << "  " << color::cyan << name << color::reset;
        }
        if (commit == "null")
            std::cout << color::gray << "  (no commits)" << color::reset;
        else
            std::cout << color::gray << "  -> commit " << commit << color::reset;
        std::cout << "\n";
    }
    return 0;
}

// ============================================================
// CMD: CHECKOUT
// ============================================================

int cmd_checkout(const std::string& name) {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    fs::path path = ION_BRANCHES + "/" + name;
    if (!fs::exists(path)) {
        std::cerr << color::red << "error: branch '" << name << "' not found\n" << color::reset;
        return 1;
    }

    std::string current = get_current_branch();
    if (name == current) {
        std::cout << color::yellow << "Already on branch " << name << "\n" << color::reset;
        return 0;
    }

    IgnoreRules rules = load_ignore_rules();

    // Warn if there are unsaved changes
    std::string head_commit = get_branch_commit(current);
    std::unordered_map<std::string, std::string> committed;
    if (head_commit != "null") {
        Commit c = read_commit(head_commit);
        committed = c.files;
    }
    auto working = collect_working_files(rules);
    bool dirty = false;
    for (auto& [f, h] : working)   { auto it = committed.find(f); if (it == committed.end() || it->second != h) { dirty = true; break; } }
    for (auto& [f, _] : committed) { if (!dirty && working.find(f) == working.end()) { dirty = true; break; } }

    if (dirty) {
        std::cout << color::yellow << "warning: you have unsaved changes.\n"
                  << "  These will be overwritten by checkout.\n"
                  << "  Continue? [y/N] " << color::reset;
        std::string resp;
        std::getline(std::cin, resp);
        if (resp != "y" && resp != "Y") {
            std::cout << color::gray << "Checkout aborted.\n" << color::reset;
            return 0;
        }
    }

    std::string commit = read_file(path.string());
    restore_commit(commit, rules);
    write_file(ION_HEAD, name);

    std::cout << color::green << "Switched to branch " << color::bold << name << color::reset << "\n";
    if (commit == "null")
        std::cout << color::gray << "  (no commits on this branch)\n" << color::reset;
    else
        std::cout << color::gray << "  at commit " << commit << "\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: HISTORY
// ============================================================

int cmd_history() {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    std::string branch = get_current_branch();
    std::string current = get_branch_commit(branch);

    std::cout << color::bold << "History of branch " << color::cyan << branch
              << color::reset << "\n\n";

    if (current == "null") {
        std::cout << color::gray << "No commits yet.\n" << color::reset;
        return 0;
    }

    int count = 0;
    std::string cid = current;

    while (cid != "null" && !cid.empty()) {
        Commit c = read_commit(cid);
        if (c.id.empty()) break;

        std::cout << color::bold << color::yellow << "commit " << c.id << color::reset;
        if (cid == current) std::cout << color::cyan << "  <-- HEAD" << color::reset;
        std::cout << "\n";

        if (!c.timestamp.empty())
            std::cout << color::gray << "  date:    " << format_timestamp(c.timestamp) << "\n" << color::reset;

        std::cout << color::bold << "  message: " << c.message << color::reset << "\n";
        std::cout << color::gray << "  files:   " << c.files.size() << " tracked\n" << color::reset;
        std::cout << "\n";

        cid = c.parent;
        ++count;
    }

    std::cout << color::gray << count << " commit(s) on this branch.\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: RESTORE (JUMP TO COMMIT)
// ============================================================

int cmd_restore(const std::string& id) {
    if (!is_valid_repo()) {
        std::cerr << color::red << "error: not an ion repository\n" << color::reset;
        return 1;
    }

    std::string commit_path = ION_COMMITS + "/" + id;
    if (!fs::exists(commit_path)) {
        std::cerr << color::red << "error: commit " << id << " not found\n" << color::reset;
        return 1;
    }

    IgnoreRules rules = load_ignore_rules();

    std::cout << color::yellow << "warning: this will overwrite your working directory.\n"
              << "  Restore commit " << id << "? [y/N] " << color::reset;
    std::string resp;
    std::getline(std::cin, resp);
    if (resp != "y" && resp != "Y") {
        std::cout << color::gray << "Restore aborted.\n" << color::reset;
        return 0;
    }

    restore_commit(id, rules);
    std::cout << color::green << "Restored commit " << id << "\n" << color::reset;
    std::cout << color::gray  << "  Note: HEAD still points to the branch tip. Use 'save' to create a new commit from here.\n" << color::reset;
    return 0;
}

// ============================================================
// CMD: LOG (ALIAS FOR HISTORY, MORE COMPACT)
// ============================================================

int cmd_log() {
    return cmd_history();
}

// ============================================================
// HELP
// ============================================================

void print_help() {
    std::cout << color::bold << "ion — a local-first version control system\n\n" << color::reset;
    std::cout << color::bold << "Usage:\n" << color::reset;
    std::cout << "  ion " << color::cyan << "init" << color::reset
              << "                      Initialize a new repository\n";
    std::cout << "  ion " << color::cyan << "save" << color::reset
              << " <message>            Save a snapshot with a message\n";
    std::cout << "  ion " << color::cyan << "save" << color::reset
              << " <message> --confirm  Save with confirmation prompt\n";
    std::cout << "  ion " << color::cyan << "status" << color::reset
              << "                    Show working directory status\n";
    std::cout << "  ion " << color::cyan << "diff" << color::reset
              << "                      Show changes vs last commit\n";
    std::cout << "  ion " << color::cyan << "diff" << color::reset
              << " <file>               Show changes for a specific file\n";
    std::cout << "  ion " << color::cyan << "history" << color::reset
              << "                   Show commit history for current branch\n";
    std::cout << "  ion " << color::cyan << "branch" << color::reset
              << " <name>              Create a new branch\n";
    std::cout << "  ion " << color::cyan << "branches" << color::reset
              << "                  List all branches\n";
    std::cout << "  ion " << color::cyan << "checkout" << color::reset
              << " <branch>          Switch to a branch\n";
    std::cout << "  ion " << color::cyan << "restore" << color::reset
              << " <commit-id>        Restore working directory to a commit\n";
    std::cout << "\n";
    std::cout << color::gray << "Ignore rules: create a .ionignore file\n"
              << "  Examples:  build/   *.log   secret.txt\n" << color::reset;
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[]) {

    if (argc < 2) {
        print_help();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "init") {
        return cmd_init();
    }
    else if (cmd == "save") {
        if (argc < 3) {
            std::cerr << color::red << "error: usage: ion save <message> [--confirm]\n" << color::reset;
            return 1;
        }
        bool confirm = (argc >= 4 && std::string(argv[3]) == "--confirm");
        return cmd_save(argv[2], confirm);
    }
    else if (cmd == "status") {
        return cmd_status();
    }
    else if (cmd == "diff") {
        std::string target = (argc >= 3) ? argv[2] : "";
        return cmd_diff(target);
    }
    else if (cmd == "history" || cmd == "log") {
        return cmd_history();
    }
    else if (cmd == "branch") {
        if (argc < 3) {
            std::cerr << color::red << "error: usage: ion branch <name>\n" << color::reset;
            return 1;
        }
        return cmd_branch(argv[2]);
    }
    else if (cmd == "branches") {
        return cmd_branches();
    }
    else if (cmd == "checkout") {
        if (argc < 3) {
            std::cerr << color::red << "error: usage: ion checkout <branch>\n" << color::reset;
            return 1;
        }
        return cmd_checkout(argv[2]);
    }
    else if (cmd == "restore") {
        if (argc < 3) {
            std::cerr << color::red << "error: usage: ion restore <commit-id>\n" << color::reset;
            return 1;
        }
        return cmd_restore(argv[2]);
    }
    else if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        print_help();
        return 0;
    }
    else {
        std::cerr << color::red << "error: unknown command '" << cmd << "'\n" << color::reset;
        std::cerr << color::gray << "Run 'ion help' to see available commands.\n" << color::reset;
        return 1;
    }
}