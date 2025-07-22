#include "MacOSSandbox.hpp"

namespace Murmur {

class MacOSSandbox::MacOSSandboxPrivate {
public:
    bool initialized = false;
};

MacOSSandbox::MacOSSandbox()
    : d(std::make_unique<MacOSSandboxPrivate>())
{
}

MacOSSandbox::~MacOSSandbox() = default;

bool MacOSSandbox::initialize() {
    d->initialized = true;
    return true;
}

void MacOSSandbox::shutdown() {
    d->initialized = false;
}

bool MacOSSandbox::isInitialized() const {
    return d->initialized;
}

} // namespace Murmur