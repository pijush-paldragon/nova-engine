#pragma once

#include <optional>
#include <string>
#include <utility>

namespace nova {

struct Error {
    std::string message;
};

// Value-or-error return type for fallible engine calls (the engine is built without exceptions).
template <class T>
class [[nodiscard]] Result {
public:
    Result(T value) : value_(std::move(value)) {}

    static Result fail(std::string message) { return Result(Error{std::move(message)}); }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T& value() { return *value_; }
    const T& value() const { return *value_; }
    const Error& error() const { return error_; }

private:
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<T> value_;
    Error error_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    static Result success() { return Result(true, {}); }
    static Result fail(std::string message) { return Result(false, Error{std::move(message)}); }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }
    const Error& error() const { return error_; }

private:
    Result(bool ok, Error error) : ok_(ok), error_(std::move(error)) {}

    bool ok_;
    Error error_;
};

} // namespace nova
