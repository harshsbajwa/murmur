#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/core.h>
#include <memory>
#include <string>

namespace Murmur {

class Logger {
public:
    enum class Level {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Critical = 5
    };
    
    static Logger& instance();
    
    void initialize(const std::string& logFilePath = "murmur.log", 
                   Level level = Level::Info);
    
    void setLevel(Level level);
    
    template<typename... Args>
    void trace(fmt::format_string<Args...> format, Args&&... args) {
        logger_->trace(format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void debug(fmt::format_string<Args...> format, Args&&... args) {
        logger_->debug(format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void info(fmt::format_string<Args...> format, Args&&... args) {
        logger_->info(format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warn(fmt::format_string<Args...> format, Args&&... args) {
        logger_->warn(format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void error(fmt::format_string<Args...> format, Args&&... args) {
        logger_->error(format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void critical(fmt::format_string<Args...> format, Args&&... args) {
        logger_->critical(format, std::forward<Args>(args)...);
    }
    
private:
    Logger() = default;
    std::shared_ptr<spdlog::logger> logger_;
};

// Convenience macros
#define MURMUR_TRACE(...) Murmur::Logger::instance().trace(__VA_ARGS__)
#define MURMUR_DEBUG(...) Murmur::Logger::instance().debug(__VA_ARGS__)
#define MURMUR_INFO(...) Murmur::Logger::instance().info(__VA_ARGS__)
#define MURMUR_WARN(...) Murmur::Logger::instance().warn(__VA_ARGS__)
#define MURMUR_ERROR(...) Murmur::Logger::instance().error(__VA_ARGS__)
#define MURMUR_CRITICAL(...) Murmur::Logger::instance().critical(__VA_ARGS__)

} // namespace Murmur