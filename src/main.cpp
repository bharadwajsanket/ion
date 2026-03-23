#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <ctime>

namespace fs = std::filesystem;

// =======================
// Helpers
// =======================

bool is_valid_repo(const fs::path& repo_path) {
    return fs::exists(repo_path / "HEAD") &&
           fs::exists(repo_path / "commits") &&
           fs::exists(repo_path / "objects");
}

std::string read_head() {
    std::ifstream head(".ion/HEAD");
    std::string value;
    std::getline(head, value);
    return value;
}

void write_head(const std::string& id) {
    std::ofstream head(".ion/HEAD", std::ios::trunc);
    head << id;
}

// =======================
// Safety
// =======================

bool confirm_action(const std::string& message) {
    std::cout << message << "\nType 'yes' to continue: ";
    std::string input;
    std::getline(std::cin, input);
    return input == "yes";
}

// =======================
// File Operations
// =======================

// Copy directory excluding .ion and build artifacts
void copy_directory(const fs::path& source, const fs::path& destination) {
    for (const auto& entry : fs::recursive_directory_iterator(source)) {

        fs::path relative = fs::relative(entry.path(), source);
        std::string rel = relative.string();

        if (rel.rfind(".ion", 0) == 0) continue;
        if (rel == "ion") continue;
        if (rel.size() >= 2 && rel.substr(rel.size() - 2) == ".o") continue;
        if (rel.size() >= 4 && rel.substr(rel.size() - 4) == ".out") continue;

        fs::path target = destination / relative;

        if (fs::is_directory(entry.path())) {
            fs::create_directories(target);
        }
        else if (fs::is_regular_file(entry.path())) {
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

// Clean working directory except .ion
void clean_working_directory() {
    for (const auto& entry : fs::directory_iterator(".")) {

        std::string name = entry.path().filename().string();

        if (name == ".ion") continue;

        fs::remove_all(entry.path());
    }
}

// =======================
// Commands
// =======================

int cmd_init() {
    fs::path repo = ".ion";
    fs::path temp_repo = ".ion.tmp";

    if (fs::exists(repo)) {
        if (is_valid_repo(repo)) {
            std::cout << "Repository already initialized.\n";
            return 0;
        } else {
            std::cerr << "Error: Invalid or corrupted repository.\n";
            return 1;
        }
    }

    try {
        if (fs::exists(temp_repo)) {
            fs::remove_all(temp_repo);
        }

        fs::create_directory(temp_repo);
        fs::create_directory(temp_repo / "commits");
        fs::create_directory(temp_repo / "objects");

        std::ofstream head(temp_repo / "HEAD");
        head << "null";
        head.close();

        fs::rename(temp_repo, repo);

        std::cout << "Initialized empty ion repository.\n";
        return 0;
    }
    catch (...) {
        std::cerr << "Error: Failed to initialize repository.\n";

        if (fs::exists(temp_repo)) {
            fs::remove_all(temp_repo);
        }

        return 1;
    }
}

int cmd_save(const std::string& message) {

    fs::path repo = ".ion";

    if (!fs::exists(repo) || !is_valid_repo(repo)) {
        std::cerr << "Error: Not an ion repository.\n";
        return 1;
    }

    std::string head = read_head();

    int new_id = 1;
    std::string parent = "null";

    if (head != "null") {
        new_id = std::stoi(head) + 1;
        parent = head;
    }

    std::string id_str = std::to_string(new_id);

    fs::path object_path = repo / "objects" / id_str;
    fs::create_directories(object_path);

    copy_directory(".", object_path);

    fs::path commit_file = repo / "commits" / id_str;

    std::ofstream commit(commit_file);
    commit << "id: " << id_str << "\n";
    commit << "parent: " << parent << "\n";
    commit << "message: " << message << "\n";
    commit << "timestamp: " << std::time(nullptr) << "\n";
    commit.close();

    write_head(id_str);

    std::cout << "Saved snapshot " << id_str << "\n";

    return 0;
}

int cmd_history() {

    fs::path repo = ".ion";

    if (!fs::exists(repo) || !is_valid_repo(repo)) {
        std::cerr << "Error: Not an ion repository.\n";
        return 1;
    }

    std::string current = read_head();

    if (current == "null") {
        std::cout << "No commits yet.\n";
        return 0;
    }

    while (current != "null") {
        fs::path commit_file = repo / "commits" / current;

        std::ifstream in(commit_file);
        std::string line, message, parent;

        while (std::getline(in, line)) {
            if (line.rfind("message:", 0) == 0)
                message = line.substr(9);
            if (line.rfind("parent:", 0) == 0)
                parent = line.substr(8);
        }

        std::cout << "Commit " << current << "\n";
        std::cout << "Message:" << message << "\n\n";

        current = parent;
    }

    return 0;
}

int cmd_restore(const std::string& id) {

    fs::path repo = ".ion";
    fs::path object_path = repo / "objects" / id;

    if (!fs::exists(repo) || !is_valid_repo(repo)) {
        std::cerr << "Error: Not an ion repository.\n";
        return 1;
    }

    if (!fs::exists(object_path)) {
        std::cerr << "Error: Commit not found.\n";
        return 1;
    }

    if (!confirm_action("Warning: This will overwrite current files.")) {
        std::cout << "Restore cancelled.\n";
        return 0;
    }

    clean_working_directory();

    for (const auto& entry : fs::recursive_directory_iterator(object_path)) {

        fs::path relative = fs::relative(entry.path(), object_path);
        fs::path target = "." / relative;

        if (fs::is_directory(entry.path())) {
            fs::create_directories(target);
        }
        else if (fs::is_regular_file(entry.path())) {
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }

    write_head(id);

    std::cout << "Restored to snapshot " << id << "\n";

    return 0;
}

// =======================
// Usage
// =======================

void print_usage() {
    std::cout << "Usage: ion <command>\n";
    std::cout << "Commands:\n";
    std::cout << "  init\n";
    std::cout << "  save <message>\n";
    std::cout << "  history\n";
    std::cout << "  restore <id>\n";
}

// =======================
// MAIN
// =======================

int main(int argc, char* argv[]) {

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "init") return cmd_init();

    else if (command == "save") {
        if (argc < 3) {
            std::cerr << "Error: Missing message\n";
            return 1;
        }
        return cmd_save(argv[2]);
    }

    else if (command == "history") return cmd_history();

    else if (command == "restore") {
        if (argc < 3) {
            std::cerr << "Error: Missing id\n";
            return 1;
        }
        return cmd_restore(argv[2]);
    }

    else {
        std::cerr << "Error: Unknown command\n";
        return 1;
    }
}