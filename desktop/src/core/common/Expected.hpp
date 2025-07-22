#pragma once

#include <type_traits>
#include <utility>
#include <stdexcept>

namespace Murmur {

// Forward declaration
template<typename E>
class Unexpected;

// Implementation similar to std::expected (C++23)
template<typename T, typename E>
class Expected {
public:
    // Note: Allowing T and E to be the same type for flexibility, like std::expected
    // Constructor disambiguation is handled through template constraints
    
    // Default constructor for default-constructible T
    template<typename U = T, typename = std::enable_if_t<std::is_default_constructible_v<U>>>
    constexpr Expected() noexcept(std::is_nothrow_default_constructible_v<T>)
        : hasValue_(true) {
        new (&value_) T();
    }
    
    // Constructors for value
    template<typename U, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<U>, Expected> && 
        !std::is_same_v<std::decay_t<U>, Unexpected<E>> &&
        std::is_constructible_v<T, U>>>
    constexpr Expected(U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : hasValue_(true) {
        new (&value_) T(std::forward<U>(value));
    }
    
    // Tag types for explicit construction when T and E are the same
    struct ValueTag {};
    struct ErrorTag {};
    
    // Explicit value constructor
    template<typename U>
    constexpr Expected(ValueTag, U&& value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : hasValue_(true) {
        new (&value_) T(std::forward<U>(value));
    }
    
    // Explicit error constructor  
    template<typename G>
    constexpr Expected(ErrorTag, G&& error) noexcept(std::is_nothrow_constructible_v<E, G>)
        : hasValue_(false) {
        new (&error_) E(std::forward<G>(error));
    }
    
    // Constructor for error when types are different or when using string literals
    template<typename G, typename = std::enable_if_t<
        std::is_constructible_v<E, G> &&
        !std::is_constructible_v<T, G> &&
        !std::is_same_v<std::decay_t<G>, Expected> &&
        !std::is_same_v<std::decay_t<G>, Unexpected<E>>>>
    constexpr Expected(const G& error) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : hasValue_(false) {
        new (&error_) E(error);
    }
    
    // Constructor from Unexpected
    template<typename G, typename = std::enable_if_t<std::is_constructible_v<E, const G&>>>
    constexpr Expected(const Unexpected<G>& unexpected) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : hasValue_(false) {
        new (&error_) E(unexpected.error());
    }
    
    template<typename G, typename = std::enable_if_t<std::is_constructible_v<E, G&&>>>
    constexpr Expected(Unexpected<G>&& unexpected) noexcept(std::is_nothrow_constructible_v<E, G&&>)
        : hasValue_(false) {
        new (&error_) E(std::move(unexpected.error()));
    }
    
    // Copy constructor
    Expected(const Expected& other) : hasValue_(other.hasValue_) {
        if (hasValue_) {
            new (&value_) T(other.value_);
        } else {
            new (&error_) E(other.error_);
        }
    }
    
    // Move constructor
    Expected(Expected&& other) noexcept : hasValue_(other.hasValue_) {
        if (hasValue_) {
            new (&value_) T(std::move(other.value_));
        } else {
            new (&error_) E(std::move(other.error_));
        }
    }
    
    // Destructor
    ~Expected() {
        if (hasValue_) {
            value_.~T();
        } else {
            error_.~E();
        }
    }
    
    // Assignment operators
    Expected& operator=(const Expected& other) {
        if (this != &other) {
            this->~Expected();
            new (this) Expected(other);
        }
        return *this;
    }
    
    Expected& operator=(Expected&& other) noexcept {
        if (this != &other) {
            this->~Expected();
            new (this) Expected(std::move(other));
        }
        return *this;
    }
    
    // Assignment from value
    template<typename U, typename = std::enable_if_t<
        !std::is_same_v<std::decay_t<U>, Expected> && 
        std::is_constructible_v<T, U> &&
        !std::is_convertible_v<U, E>>>
    Expected& operator=(U&& value) {
        this->~Expected();
        new (this) Expected(std::forward<U>(value));
        return *this;
    }
    
    // Assignment from Unexpected
    template<typename G>
    Expected& operator=(const Unexpected<G>& unexpected) {
        this->~Expected();
        new (this) Expected(unexpected);
        return *this;
    }
    
    template<typename G>
    Expected& operator=(Unexpected<G>&& unexpected) {
        this->~Expected();
        new (this) Expected(std::move(unexpected));
        return *this;
    }
    
    // Access methods
    constexpr bool hasValue() const noexcept { return hasValue_; }
    constexpr bool hasError() const noexcept { return !hasValue_; }
    
    constexpr explicit operator bool() const noexcept { return hasValue(); }
    
    constexpr const T& value() const& {
        if (!hasValue_) {
            throw std::runtime_error("Expected contains error, not value");
        }
        return value_;
    }
    
    constexpr T& value() & {
        if (!hasValue_) {
            throw std::runtime_error("Expected contains error, not value");
        }
        return value_;
    }
    
    constexpr T&& value() && {
        if (!hasValue_) {
            throw std::runtime_error("Expected contains error, not value");
        }
        return std::move(value_);
    }
    
    constexpr const E& error() const& {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return error_;
    }
    
    constexpr E& error() & {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return error_;
    }
    
    constexpr E&& error() && {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return std::move(error_);
    }
    
    // Monadic operations
    template<typename F>
    constexpr auto andThen(F&& f) const& -> Expected<std::invoke_result_t<F, const T&>, E> {
        if (hasValue()) {
            return std::forward<F>(f)(value());
        }
        return error();
    }
    
    template<typename F>
    constexpr auto orElse(F&& f) const& -> Expected<T, std::invoke_result_t<F, const E&>> {
        if (hasError()) {
            return std::forward<F>(f)(error());
        }
        return value();
    }
    
    template<typename F>
    constexpr auto transform(F&& f) const& -> Expected<std::invoke_result_t<F, const T&>, E> {
        if (hasValue()) {
            return Expected<std::invoke_result_t<F, const T&>, E>(std::forward<F>(f)(value()));
        }
        return Expected<std::invoke_result_t<F, const T&>, E>(error());
    }
    
    // Convenience methods
    template<typename U>
    constexpr T valueOr(U&& defaultValue) const& {
        return hasValue() ? value() : static_cast<T>(std::forward<U>(defaultValue));
    }
    
    template<typename U>
    constexpr T valueOr(U&& defaultValue) && {
        return hasValue() ? std::move(value()) : static_cast<T>(std::forward<U>(defaultValue));
    }

private:
    bool hasValue_;
    union {
        T value_;
        E error_;
    };
};

// Helper class for creating error instances
template<typename E>
class Unexpected {
public:
    constexpr explicit Unexpected(const E& error) : error_(error) {}
    constexpr explicit Unexpected(E&& error) : error_(std::move(error)) {}
    
    constexpr const E& error() const& { return error_; }
    constexpr E& error() & { return error_; }
    constexpr E&& error() && { return std::move(error_); }
    
private:
    E error_;
};

// Helper functions for creating Expected instances
template<typename T>
constexpr Expected<std::decay_t<T>, void> makeExpected(T&& value) {
    return Expected<std::decay_t<T>, void>(std::forward<T>(value));
}

// Helper for creating Expected with explicit value when T and E are same type
template<typename T, typename E>
constexpr Expected<T, E> makeExpectedValue(T&& value) {
    return Expected<T, E>(typename Expected<T, E>::ValueTag{}, std::forward<T>(value));
}

template<typename E>
constexpr Unexpected<std::decay_t<E>> makeUnexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

// Template specialization for Expected<void, E>
template<typename E>
class Expected<void, E> {
public:
    // Constructor for success (void)
    constexpr Expected() noexcept : hasValue_(true) {}
    
    // Constructor for error
    template<typename G = E>
    constexpr Expected(const G& error) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : hasValue_(false), error_(error) {}
        
    template<typename G = E>
    constexpr Expected(G&& error) noexcept(std::is_nothrow_constructible_v<E, G&&>)
        : hasValue_(false), error_(std::forward<G>(error)) {}
        
    // Constructor from Unexpected
    template<typename G>
    constexpr Expected(const Unexpected<G>& unexpected) noexcept(std::is_nothrow_constructible_v<E, const G&>)
        : hasValue_(false), error_(unexpected.error()) {}
    
    template<typename G>
    constexpr Expected(Unexpected<G>&& unexpected) noexcept(std::is_nothrow_constructible_v<E, G&&>)
        : hasValue_(false), error_(std::move(unexpected.error())) {}
    
    // Copy constructor
    Expected(const Expected& other) : hasValue_(other.hasValue_) {
        if (!hasValue_) {
            new (&error_) E(other.error_);
        }
    }
    
    // Move constructor
    Expected(Expected&& other) noexcept : hasValue_(other.hasValue_) {
        if (!hasValue_) {
            new (&error_) E(std::move(other.error_));
        }
    }
    
    // Destructor
    ~Expected() {
        if (!hasValue_) {
            error_.~E();
        }
    }
    
    // Assignment operators
    Expected& operator=(const Expected& other) {
        if (this != &other) {
            this->~Expected();
            new (this) Expected(other);
        }
        return *this;
    }
    
    Expected& operator=(Expected&& other) noexcept {
        if (this != &other) {
            this->~Expected();
            new (this) Expected(std::move(other));
        }
        return *this;
    }
    
    // Access methods
    constexpr bool hasValue() const noexcept { return hasValue_; }
    constexpr bool hasError() const noexcept { return !hasValue_; }
    
    constexpr explicit operator bool() const noexcept { return hasValue(); }
    
    constexpr void value() const& {
        if (!hasValue_) {
            throw std::runtime_error("Expected contains error, not value");
        }
    }
    
    constexpr const E& error() const& {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return error_;
    }
    
    constexpr E& error() & {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return error_;
    }
    
    constexpr E&& error() && {
        if (hasValue_) {
            throw std::runtime_error("Expected contains value, not error");
        }
        return std::move(error_);
    }

private:
    bool hasValue_;
    union {
        E error_;
    };
};

} // namespace Murmur