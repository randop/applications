#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "dispatcher/task_dispatcher.hpp"
#include "dispatcher/abort_controller.hpp"
#include "net/http_client.hpp"
#include "net/git_client.hpp"

using namespace task_dispatcher;

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " <command> [options]" << std::endl;
    std::cerr << "Commands:" << std::endl;
    std::cerr << "  download <url> <output>    Download file from URL" << std::endl;
    std::cerr << "  clone <repo> <path>        Clone git repository" << std::endl;
    std::cerr << "  fetch <repo>               Fetch git repository updates" << std::endl;
    std::cerr << "  test                       Run test suite" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --timeout <seconds>        Operation timeout (default: 30)" << std::endl;
    std::cerr << "  --branch <branch>          Git branch (default: main)" << std::endl;
    std::cerr << "  --shallow                  Shallow clone (depth=1)" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    
    // Parse arguments
    std::string url_or_repo;
    std::string output_path;
    std::chrono::seconds timeout{30};
    std::string branch = "main";
    bool shallow = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--timeout" && i + 1 < argc) {
            timeout = std::chrono::seconds(std::stoi(argv[++i]));
        } else if (arg == "--branch" && i + 1 < argc) {
            branch = argv[++i];
        } else if (arg == "--shallow") {
            shallow = true;
        } else if (url_or_repo.empty()) {
            url_or_repo = arg;
        } else if (output_path.empty()) {
            output_path = arg;
        }
    }

    try {
        // Initialize io_context
        boost::asio::io_context ioc;
        boost::asio::ssl::context ssl_ctx{boost::asio::ssl::context::tlsv12_client};
        
        // Create abort controller with timeout
        auto abort_controller = AbortController::create(timeout);
        
        // Create task dispatcher
        TaskDispatcher dispatcher;
        
        if (command == "download") {
            if (url_or_repo.empty() || output_path.empty()) {
                print_usage(argv[0]);
                return 1;
            }
            
            std::cout << "Downloading: " << url_or_repo << std::endl;
            std::cout << "Output: " << output_path << std::endl;
            
            auto http_client = std::make_shared<net::HttpClient>(ioc, ssl_ctx);
            
            // Schedule download task
            auto task_id = dispatcher.schedule_with_abort(
                [&](auto ac) {
                    boost::asio::co_spawn(ioc, 
                        http_client->download(url_or_repo, output_path, ac, 
                            [](size_t dl, size_t total) {
                                std::cout << "\rProgress: " << dl << "/" << total 
                                          << " bytes (" << (100 * dl / total) << "%)" << std::flush;
                            }),
                        boost::asio::detached);
                },
                abort_controller
            );
            
            std::cout << "Task scheduled with ID: " << task_id << std::endl;
            
        } else if (command == "clone") {
            if (url_or_repo.empty() || output_path.empty()) {
                print_usage(argv[0]);
                return 1;
            }
            
            std::cout << "Cloning: " << url_or_repo << std::endl;
            std::cout << "To: " << output_path << std::endl;
            if (shallow) {
                std::cout << "Shallow clone enabled" << std::endl;
            }
            
            auto git_client = std::make_shared<net::GitClient>(ioc);
            
            if (shallow) {
                auto task_id = dispatcher.schedule_with_abort(
                    [&](auto ac) {
                        boost::asio::co_spawn(ioc,
                            git_client->shallow_clone(url_or_repo, output_path, ac,
                                [](const net::GitProgress& prog) {
                                    std::cout << "\r" << prog.message 
                                              << " " << prog.percentage << "%" << std::flush;
                                }),
                            boost::asio::detached);
                    },
                    abort_controller
                );
                std::cout << "Shallow clone task scheduled with ID: " << task_id << std::endl;
            } else {
                auto task_id = dispatcher.schedule_with_abort(
                    [&](auto ac) {
                        boost::asio::co_spawn(ioc,
                            git_client->clone(url_or_repo, output_path, ac,
                                [](const net::GitProgress& prog) {
                                    std::cout << "\r" << prog.message 
                                              << " " << prog.percentage << "%" << std::flush;
                                }, branch),
                            boost::asio::detached);
                    },
                    abort_controller
                );
                std::cout << "Clone task scheduled with ID: " << task_id << std::endl;
            }
            
        } else if (command == "fetch") {
            if (url_or_repo.empty()) {
                print_usage(argv[0]);
                return 1;
            }
            
            std::cout << "Fetching: " << url_or_repo << std::endl;
            
            auto git_client = std::make_shared<net::GitClient>(ioc);
            
            auto task_id = dispatcher.schedule_with_abort(
                [&](auto ac) {
                    boost::asio::co_spawn(ioc,
                        git_client->fetch(url_or_repo, ac),
                        boost::asio::detached);
                },
                abort_controller
            );
            
            std::cout << "Fetch task scheduled with ID: " << task_id << std::endl;
            
        } else if (command == "test") {
            std::cout << "Running tests..." << std::endl;
            
            // Test abort controller
            auto test_ac = AbortController::create(std::chrono::milliseconds(1000));
            
            auto test_task = dispatcher.schedule_with_abort(
                [test_ac](auto ac) {
                    std::cout << "Test task started" << std::endl;
                    ac->signal()->wait_for_abort();
                    std::cout << "Test task aborted: " << ac->signal()->reason().value_or("unknown") << std::endl;
                },
                test_ac
            );
            
            std::cout << "Test task scheduled with ID: " << test_task << std::endl;
            std::cout << "Will auto-abort after 1 second" << std::endl;
            
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        
        // Run the event loop
        std::cout << "Starting event loop..." << std::endl;
        dispatcher.run();
        
        // Run io_context
        ioc.run();
        
        std::cout << std::endl << "Done!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
