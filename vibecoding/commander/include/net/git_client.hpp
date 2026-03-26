#pragma once

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <coroutine>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "dispatcher/abort_controller.hpp"
#include "net/http_client.hpp"

namespace task_dispatcher::net {

namespace asio = boost::asio;

// Git operation result
struct GitResult {
    bool success{false};
    std::string output;
    std::string error_output;
    int exit_code{0};
    std::error_code error;
    bool aborted{false};
    
    bool ok() const noexcept {
        return !aborted && !error && exit_code == 0 && success;
    }
};

// Git clone/fetch progress
struct GitProgress {
    enum class Stage {
        RECEIVING_OBJECTS,
        RESOLVING_DELTAS,
        CHECKING_OUT,
        COMPRESSING_OBJECTS
    };
    
    Stage stage;
    int percentage{0};  // 0-100
    size_t processed{0};
    size_t total{0};
    std::string message;
};

using GitProgressCallback = std::function<void(const GitProgress&)>;

// Git client for repository operations
class GitClient : public std::enable_shared_from_this<GitClient> {
public:
    explicit GitClient(asio::io_context& ioc);
    ~GitClient();

    // Non-copyable
    GitClient(const GitClient&) = delete;
    GitClient& operator=(const GitClient&) = delete;

    // Check if git is available
    static bool is_git_available();
    
    // Get git version
    static std::string get_git_version();

    // Clone repository
    asio::awaitable<GitResult> clone(
        std::string_view repo_url,
        std::string_view destination_path,
        std::shared_ptr<AbortController> abort_controller = nullptr,
        GitProgressCallback progress = nullptr,
        std::string_view branch = ""  // empty for default branch
    );

    // Shallow clone (depth=1)
    asio::awaitable<GitResult> shallow_clone(
        std::string_view repo_url,
        std::string_view destination_path,
        std::shared_ptr<AbortController> abort_controller = nullptr,
        GitProgressCallback progress = nullptr
    );

    // Fetch updates
    asio::awaitable<GitResult> fetch(
        std::string_view repo_path,
        std::shared_ptr<AbortController> abort_controller = nullptr
    );

    // Checkout specific commit/branch/tag
    asio::awaitable<GitResult> checkout(
        std::string_view repo_path,
        std::string_view ref,
        std::shared_ptr<AbortController> abort_controller = nullptr
    );

    // Get remote HEAD commit
    asio::awaitable<GitResult> get_remote_head(
        std::string_view repo_url,
        std::string_view branch = "HEAD",
        std::shared_ptr<AbortController> abort_controller = nullptr
    );

    // Set git executable path
    void set_git_executable(std::string_view path);

    // Set credentials (for private repos)
    void set_credentials(std::string_view username, std::string_view password);

    // Enable/disable progress output
    void set_verbose(bool verbose);

private:
    asio::awaitable<GitResult> run_git_command(
        const std::vector<std::string>& args,
        std::shared_ptr<AbortController> abort_controller,
        GitProgressCallback progress = nullptr,
        std::string_view working_dir = ""
    );

    void parse_progress_line(const std::string& line, GitProgressCallback callback);
    std::vector<std::string> build_clone_args(std::string_view repo_url, std::string_view dest, std::string_view branch);

    asio::io_context& ioc_;
    std::string git_executable_{"git"};
    std::string username_;
    std::string password_;
    bool verbose_{true};
    std::chrono::seconds default_timeout_{300};  // 5 minutes
};

// Download from GitHub/GitLab raw URL
asio::awaitable<HttpResponse> download_from_git_host(
    asio::io_context& ioc,
    boost::asio::ssl::context& ssl_ctx,
    std::string_view git_url,
    std::string_view output_path,
    std::shared_ptr<AbortController> abort_controller = nullptr,
    DownloadProgressCallback progress = nullptr
);

// Convert git URL to raw content URL
std::string to_raw_content_url(
    std::string_view git_url,
    std::string_view ref = "main"
);

} // namespace task_dispatcher::net
