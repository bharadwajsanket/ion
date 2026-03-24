#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// =======================
// FILE HELPERS
// =======================

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::string value;
    std::getline(in, value);
    return value;
}

void write_file(const std::string& path, const std::string& value) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << value;
}

// =======================
// REPO CHECK
// =======================

bool is_valid_repo() {
    return fs::exists(".ion/HEAD") &&
           fs::exists(".ion/branches") &&
           fs::exists(".ion/commits") &&
           fs::exists(".ion/objects/files");
}

// =======================
// BRANCH HELPERS
// =======================

std::string get_current_branch() {
    return read_file(".ion/HEAD");
}

std::string get_branch_commit(const std::string& branch) {
    return read_file(".ion/branches/" + branch);
}

void set_branch_commit(const std::string& branch, const std::string& commit) {
    write_file(".ion/branches/" + branch, commit);
}

// =======================
// HASH
// =======================

std::string hash_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return std::to_string(std::hash<std::string>{}(buffer.str()));
}

// =======================
// IGNORE
// =======================

bool should_ignore(const std::string& rel) {
    if (rel.rfind(".ion", 0) == 0) return true;
    return false;
}

// =======================
// COMMIT READ
// =======================

std::unordered_map<std::string, std::string> read_commit_map(const std::string& id) {

    std::unordered_map<std::string, std::string> map;

    if (id == "null") return map;

    std::ifstream in(".ion/commits/" + id);
    std::string line;

    bool reading = false;

    while (std::getline(in, line)) {

        if (line == "files:") {
            reading = true;
            continue;
        }

        if (reading) {
            std::istringstream iss(line);
            std::string file, hash;
            iss >> file >> hash;
            map[file] = hash;
        }
    }

    return map;
}

// =======================
// RESTORE
// =======================

void restore_commit(const std::string& id) {

    for (auto& entry : fs::directory_iterator(".")) {
        if (entry.path().filename() == ".ion") continue;
        fs::remove_all(entry.path());
    }

    if (id == "null") return;

    std::ifstream in(".ion/commits/" + id);
    std::string line;
    bool reading = false;

    while (std::getline(in, line)) {

        if (line == "files:") {
            reading = true;
            continue;
        }

        if (reading) {
            std::istringstream iss(line);
            std::string file, hash;
            iss >> file >> hash;

            fs::path src = ".ion/objects/files/" + hash;
            fs::path dst = file;

            fs::create_directories(dst.parent_path());
            fs::copy_file(src, dst);
        }
    }
}

// =======================
// INIT
// =======================

int cmd_init() {

    if (fs::exists(".ion")) {
        std::cout << "Repository already exists\n";
        return 0;
    }

    fs::create_directory(".ion");
    fs::create_directory(".ion/commits");
    fs::create_directories(".ion/objects/files");
    fs::create_directory(".ion/branches");

    write_file(".ion/HEAD", "main");
    write_file(".ion/branches/main", "null");

    std::cout << "Initialized ion repository\n";
    return 0;
}

// =======================
// SAVE (BRANCH-AWARE)
// =======================

int cmd_save(const std::string& message) {

    if (!is_valid_repo()) {
        std::cerr << "Not an ion repository\n";
        return 1;
    }

    std::string branch = get_current_branch();
    std::string parent = get_branch_commit(branch);

    int new_id = (parent == "null") ? 1 : std::stoi(parent) + 1;
    std::string id = std::to_string(new_id);

    std::vector<std::pair<std::string, std::string>> file_map;

    for (const auto& entry : fs::recursive_directory_iterator(".")) {

        if (!fs::is_regular_file(entry.path())) continue;

        std::string rel = fs::relative(entry.path(), ".").string();

        if (should_ignore(rel)) continue;

        std::string hash = hash_file(entry.path());
        fs::path obj = ".ion/objects/files/" + hash;

        if (!fs::exists(obj)) {
            fs::create_directories(obj.parent_path());
            fs::copy_file(entry.path(), obj);
        }

        file_map.push_back({rel, hash});
    }

    std::ofstream commit(".ion/commits/" + id);
    commit << "id: " << id << "\n";
    commit << "parent: " << parent << "\n";
    commit << "message: " << message << "\n";
    commit << "timestamp: " << std::time(nullptr) << "\n";
    commit << "files:\n";

    for (auto& [f, h] : file_map) {
        commit << f << " " << h << "\n";
    }

    set_branch_commit(branch, id);

    std::cout << "Saved snapshot " << id << " on branch " << branch << "\n";
    return 0;
}

// =======================
// BRANCH
// =======================

int cmd_branch(const std::string& name) {

    fs::create_directories(".ion/branches");

    fs::path path = ".ion/branches/" + name;

    if (fs::exists(path)) {
        std::cerr << "Branch already exists\n";
        return 1;
    }

    std::string current = get_current_branch();
    std::string commit = get_branch_commit(current);

    write_file(path.string(), commit);

    std::cout << "Created branch " << name << "\n";
    return 0;
}

// =======================
// BRANCH LIST
// =======================

int cmd_branches() {

    std::string current = get_current_branch();

    for (auto& entry : fs::directory_iterator(".ion/branches")) {

        std::string name = entry.path().filename();

        if (name == current) {
            std::cout << "* " << name << "\n";
        } else {
            std::cout << "  " << name << "\n";
        }
    }

    return 0;
}

// =======================
// CHECKOUT
// =======================

int cmd_checkout(const std::string& name) {

    fs::path path = ".ion/branches/" + name;

    if (!fs::exists(path)) {
        std::cerr << "Branch not found\n";
        return 1;
    }

    std::string commit = read_file(path.string());

    restore_commit(commit);
    write_file(".ion/HEAD", name);

    std::cout << "Switched to branch " << name << "\n";
    return 0;
}

// =======================
// HISTORY (PER BRANCH)
// =======================

int cmd_history() {

    std::string branch = get_current_branch();
    std::string current = get_branch_commit(branch);

    if (current == "null") {
        std::cout << "No commits yet\n";
        return 0;
    }

    while (current != "null") {

        std::ifstream in(".ion/commits/" + current);
        std::string line;

        std::string message;
        std::string parent;

        while (std::getline(in, line)) {
            if (line.rfind("message:", 0) == 0) {
                message = line.substr(9);
            }
            if (line.rfind("parent:", 0) == 0) {
                parent = line.substr(8);
            }
        }

        std::cout << "commit " << current << "\n";
        std::cout << "    " << message << "\n\n";

        current = parent;
    }

    return 0;
}

// =======================
// MAIN
// =======================

int main(int argc, char* argv[]) {

    if (argc < 2) {
        std::cout << "Usage: ion <command>\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "init") return cmd_init();

    else if (cmd == "save") return cmd_save(argv[2]);

    else if (cmd == "branch") return cmd_branch(argv[2]);

    else if (cmd == "branches") return cmd_branches();

    else if (cmd == "checkout") return cmd_checkout(argv[2]);

    else if (cmd == "history") return cmd_history();

    else {
        std::cerr << "Unknown command\n";
        return 1;
    }
}