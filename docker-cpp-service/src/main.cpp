#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <ctime>

// Atomic flag to handle graceful shutdown
std::atomic<bool> running{true};

std::string get_timestamp() {
    auto now = std::time(nullptr);
    std::string timestamp = std::ctime(&now);
    timestamp.pop_back(); // Remove trailing newline
    return timestamp;
}

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    std::cout << get_timestamp() << " [WARN] Received signal " << signal << ", shutting down..." << std::endl;
    std::cerr << get_timestamp() << " [WARN] Signal " << signal << " error log." << std::endl;
    std::cout.flush(); // Flush stdout
    std::cerr.flush();
    running = false;
}

int main() {
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Open log file in append mode
    std::ofstream log_file("/var/log/service.log", std::ios::app);
    if (!log_file.is_open()) {
        std::cerr << get_timestamp() << " [ERROR] Failed to open log file" << std::endl;
        return 1;
    }

    log_file << get_timestamp() << " [INFO] Service started" << std::endl;

    try {
        while (running) {
            // Simulate work (e.g., processing tasks)
            log_file << get_timestamp() << " [INFO] Service is running" << std::endl;
            log_file.flush(); // Ensure logs are written immediately

            // Sleep for 5 seconds
            std::this_thread::sleep_for(std::chrono::seconds(5));

            // Simulate a potential crash (for testing, uncomment below)
            // if (rand() % 100 < 10) throw std::runtime_error("Simulated crash");
        }
    } catch (const std::exception& e) {
        log_file << get_timestamp() << " [ERROR] Exception: " << e.what() << std::endl;
        log_file.flush();
        log_file.close();
        return 1; // Exit with error to trigger s6 restart
    }

    log_file << get_timestamp() << " [INFO] Service shutting down" << std::endl;
    log_file.flush();
    log_file.close();
    return 0;
}
