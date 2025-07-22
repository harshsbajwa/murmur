#pragma once

#include <memory>

namespace Murmur {

class MacOSSandbox {
public:
    MacOSSandbox();
    ~MacOSSandbox();

    bool initialize();
    void shutdown();
    bool isInitialized() const;

private:
    class MacOSSandboxPrivate;
    std::unique_ptr<MacOSSandboxPrivate> d;
};

} // namespace Murmur