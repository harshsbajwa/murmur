#include "Logger.hpp"
#include <spdlog/pattern_formatter.h>

namespace Murmur {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::initialize(const std::string& logFilePath, Level level) {
    try {
        // Create sinks
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath, 1024 * 1024 * 5, 3); // 5MB, 3 files
        
        // Configure formatting
        console_sink->set_pattern("[%H:%M:%S] [%^%l%$] [%t] %v");
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f] [%l] [%t] [%s:%#] %v");
        
        // Create logger
        logger_ = std::make_shared<spdlog::logger>("murmur", 
            spdlog::sinks_init_list{console_sink, file_sink});
        
        // Set level
        setLevel(level);
        
        // Register globally
        spdlog::register_logger(logger_);
        
        MURMUR_INFO("Logger initialized with file: {}", logFilePath);
        
    } catch (const spdlog::spdlog_ex& ex) {
        // Fallback to console only
        logger_ = spdlog::stdout_color_mt("murmur_fallback");
        logger_->error("Logger initialization failed: {}", ex.what());
    }
}

void Logger::setLevel(Level level) {
    if (logger_) {
        logger_->set_level(static_cast<spdlog::level::level_enum>(level));
    }
}

} // namespace Murmur