#include "net/git_client.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace task_dispatcher::net {

GitClient::GitClient(asio::io_context& ioc)
    : ioc_(ioc)
{
}

GitClient::~GitClient() = default;

bool GitClient::is_git_available() {
    int result = std::system("git --version > /dev/null 2>&1");
    return result == 0;
}

std::string GitClient::get_git_version() {
    FILE* pipe = popen("git --version 2>&1", "r");
    if (!pipe) return "unknown";
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    // Remove newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    return result;
}

asio::awaitable<GitResult> GitClient::clone(
    std::string_view repo_url,
    std::string_view destination_path,
    std::shared_ptr<AbortController> abort_controller,
    GitProgressCallback progress,
    std::string_view branch
) {
    auto args = build_clone_args(repo_url, destination_path, branch);
    co_return co_await run_git_command(args, abort_controller, progress);
}

asio::awaitable<GitResult> GitClient::shallow_clone(
    std::string_view repo_url,
    std::string_view destination_path,
    std::shared_ptr<AbortController> abort_controller,
    GitProgressCallback progress
) {
    std::vector<std::string> args = {
        "clone",
        "--depth", "1",
        "--progress",
        std::string(repo_url),
        std::string(destination_path)
    };
    
    co_return co_await run_git_command(args, abort_controller, progress);
}

asio::awaitable<GitResult> GitClient::fetch(
    std::string_view repo_path,
    std::shared_ptr<AbortController> abort_controller
) {
    std::vector<std::string> args = {"fetch", "--progress", "origin"};
    co_return co_await run_git_command(args, abort_controller, nullptr, repo_path);
}

asio::awaitable<GitResult> GitClient::checkout(
    std::string_view repo_path,
    std::string_view ref,
    std::shared_ptr<AbortController> abort_controller
) {
    std::vector<std::string> args = {"checkout", std::string(ref)};
    co_return co_await run_git_command(args, abort_controller, nullptr, repo_path);
}

asio::awaitable<GitResult> GitClient::get_remote_head(
    std::string_view repo_url,
    std::string_view branch,
    std::shared_ptr<AbortController> abort_controller
) {
    std::vector<std::string> args = {
        "ls-remote",
        std::string(repo_url),
        std::string(branch)
    };
    
    auto result = co_await run_git_command(args, abort_controller);
    
    if (result.ok() && !result.output.empty()) {
        // Parse output: "<sha>\t<ref>"
        std::istringstream iss(result.output);
        std::string sha;
        if (iss >> sha) {
            result.output = sha;
        }
    }
    
    co_return result;
}

void GitClient::set_git_executable(std::string_view path) {
    git_executable_ = std::string(path);
}

void GitClient::set_credentials(std::string_view username, std::string_view password) {
    username_ = std::string(username);
    password_ = std::string(password);
}

void GitClient::set_verbose(bool verbose) {
    verbose_ = verbose;
}

asio::awaitable<GitResult> GitClient::run_git_command(
    const std::vector<std::string>& args,
    std::shared_ptr<AbortController> abort_controller,
    GitProgressCallback progress,
    std::string_view working_dir
) {
    GitResult result;
    
    if (abort_controller && abort_controller->is_aborted()) {
        result.aborted = true;
        co_return result;
    }
    
    // Build command
    std::string cmd = git_executable_;
    for (const auto& arg : args) {
        cmd += " ";
        // Simple escaping for arguments with spaces
        if (arg.find(' ') != std::string::npos) {
            cmd += "\"" + arg + "\"";
        } else {
            cmd += arg;
        }
    }
    
    if (verbose_) {
        std::cout << "Executing: " << cmd << std::endl;
    }
    
    // Set working directory if specified
    std::string original_cwd;
    if (!working_dir.empty()) {
        char* cwd = getcwd(nullptr, 0);
        if (cwd) {
            original_cwd = cwd;
            free(cwd);
            chdir(std::string(working_dir).c_str());
        }
    }
    
    // Execute git command
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        result.error = std::make_error_code(std::errc::io_error);
        if (!working_dir.empty() && !original_cwd.empty()) {
            chdir(original_cwd.c_str());
        }
        co_return result;
    }
    
    char buffer[4096];
    std::string line;
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        // Check abort
        if (abort_controller && abort_controller->is_aborted()) {
            pclose(pipe);
            result.aborted = true;
            if (!working_dir.empty() && !original_cwd.empty()) {
                chdir(original_cwd.c_str());
            }
            co_return result;
        }
        
        line = buffer;
        
        // Parse progress if callback provided
        if (progress && !line.empty()) {
            parse_progress_line(line, progress);
        }
        
        // Accumulate output
        if (verbose_) {
            std::cout << line;
        }
        result.output += line;
        
        // Yield control occasionally
        co_await asio::post(ioc_, asio::use_awaitable);
    }
    
    result.exit_code = pclose(pipe);
    result.success = (result.exit_code == 0);
    
    // Restore working directory
    if (!working_dir.empty() && !original_cwd.empty()) {
        chdir(original_cwd.c_str());
    }
    
    co_return result;
}

void GitClient::parse_progress_line(const std::string& line, GitProgressCallback callback) {
    // Parse common git progress patterns
    // Example: "Receiving objects: 45% (1234/2743), 1.23 MiB | 1.23 MiB/s"
    
    GitProgress prog;
    
    static const std::regex receiving_regex(
        R"(receiving objects:\s*(\d+)%\s*\((\d+)/(\d+)\))",
        std::regex::icase
    );
    static const std::regex resolving_regex(
        R"(resolving deltas:\s*(\d+)%\s*\((\d+)/(\d+)\))",
        std::regex::icase
    );
    
    std::smatch match;
    
    if (std::regex_search(line, match, receiving_regex)) {
        prog.stage = GitProgress::Stage::RECEIVING_OBJECTS;
        prog.percentage = std::stoi(match[1]);
        prog.processed = std::stoul(match[2]);
        prog.total = std::stoul(match[3]);
        prog.message = "Receiving objects";
        callback(prog);
    } else if (std::regex_search(line, match, resolving_regex)) {
        prog.stage = GitProgress::Stage::RESOLVING_DELTAS;
        prog.percentage = std::stoi(match[1]);
        prog.processed = std::stoul(match[2]);
        prog.total = std::stoul(match[3]);
        prog.message = "Resolving deltas";
        callback(prog);
    }
}

std::vector<std::string> GitClient::build_clone_args(
    std::string_view repo_url,
    std::string_view dest,
    std::string_view branch
) {
    std::vector<std::string> args = {"clone", "--progress"};
    
    if (!branch.empty()) {
        args.push_back("--branch");
        args.push_back(std::string(branch));
    }
    
    args.push_back(std::string(repo_url));
    args.push_back(std::string(dest));
    
    return args;
}

asio::awaitable<HttpResponse> download_from_git_host(
    asio::io_context& ioc,
    boost::asio::ssl::context& ssl_ctx,
    std::string_view git_url,
    std::string_view output_path,
    std::shared_ptr<AbortController> abort_controller,
    DownloadProgressCallback progress
) {
    // Convert git URL to raw content URL if possible
    std::string raw_url = to_raw_content_url(git_url, "main");
    
    HttpClient client(ioc, ssl_ctx);
    co_return co_await client.download(raw_url, output_path, abort_controller, progress);
}

std::string to_raw_content_url(std::string_view git_url, std::string_view ref) {
    std::string url(git_url);
    
    // Convert GitHub URL
    // https://github.com/user/repo/blob/main/path/file -> https://raw.githubusercontent.com/user/repo/main/path/file
    static const std::regex github_regex(
        R"(https?://github\.com/([^/]+)/([^/]+)/blob/([^/]+)/(.+))",
        std::regex::icase
    );
    
    std::smatch match;
    if (std::regex_match(url, match, github_regex)) {
        return "https://raw.githubusercontent.com/" + 
               std::string(match[1]) + "/" + 
               std::string(match[2]) + "/" + 
               std::string(match[3]) + "/" + 
               std::string(match[4]);
    }
    
    // Convert GitLab URL
    // https://gitlab.com/user/repo/-/blob/main/path/file -> https://gitlab.com/user/repo/raw/main/path/file
    static const std::regex gitlab_regex(
        R"(https?://gitlab\.com/([^/]+/[^/]+)/-/blob/([^/]+)/(.+))",
        std::regex::icase
    );
    
    if (std::regex_match(url, match, gitlab_regex)) {
        return "https://gitlab.com/" + 
               std::string(match[1]) + 
               "/raw/" + 
               std::string(match[2]) + "/" + 
               std::string(match[3]);
    }
    
    // If no conversion matched, return original
    return url;
}

} // namespace task_dispatcher::net
