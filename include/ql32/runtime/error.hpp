#pragma once

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ql32::runtime {

// Stable QL/32 language errors. Zero always means success; positive values
// are assigned consecutively and are part of the BASIC-facing contract. Add
// new codes at the end so existing numeric values remain stable.
enum class ErrorCode : std::int32_t {
    None = 0,
    GeneralFailure = 1,
    InvalidArgument = 2,
    Syntax = 3,
    TypeMismatch = 4,
    OutOfRange = 5,
    NumericOverflow = 6,
    DivisionByZero = 7,
    OutOfMemory = 8,
    InvalidPath = 9,
    NotFound = 10,
    PermissionDenied = 11,
    IoError = 12,
    ServiceUnavailable = 13,
    InvalidHandle = 14,
    InvalidTaskId = 15,
    InvalidTaskState = 16,
    TaskCancelled = 17,
    TaskLimit = 18,
    UnsupportedOperation = 19,
    InvalidSelector = 20,
    TaskStillRunning = 21
};

enum class ServiceErrorCode {
    InvalidPath,
    NotFound,
    PermissionDenied,
    IoError,
    Unavailable,
    InvalidHandle
};

inline ErrorCode languageErrorCode(ServiceErrorCode code) noexcept {
    switch (code) {
    case ServiceErrorCode::InvalidPath: return ErrorCode::InvalidPath;
    case ServiceErrorCode::NotFound: return ErrorCode::NotFound;
    case ServiceErrorCode::PermissionDenied: return ErrorCode::PermissionDenied;
    case ServiceErrorCode::IoError: return ErrorCode::IoError;
    case ServiceErrorCode::Unavailable: return ErrorCode::ServiceUnavailable;
    case ServiceErrorCode::InvalidHandle: return ErrorCode::InvalidHandle;
    }
    return ErrorCode::GeneralFailure;
}

class ServiceError final : public std::runtime_error {
public:
    ServiceError(ServiceErrorCode code, std::string message)
        : std::runtime_error(message), code_(code), detail_(std::move(message)) {}

    ServiceErrorCode code() const noexcept { return code_; }
    ErrorCode languageCode() const noexcept { return languageErrorCode(code_); }
    const std::string& detail() const noexcept { return detail_; }

private:
    ServiceErrorCode code_;
    std::string detail_;
};

struct ErrorInfo {
    ErrorCode code = ErrorCode::None;
    std::string detail;
};

inline const char* errorText(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::None: return "No error";
    case ErrorCode::GeneralFailure: return "General failure";
    case ErrorCode::InvalidArgument: return "Invalid argument";
    case ErrorCode::Syntax: return "Syntax error";
    case ErrorCode::TypeMismatch: return "Type mismatch";
    case ErrorCode::OutOfRange: return "Value out of range";
    case ErrorCode::NumericOverflow: return "Numeric overflow";
    case ErrorCode::DivisionByZero: return "Division by zero";
    case ErrorCode::OutOfMemory: return "Out of memory";
    case ErrorCode::InvalidPath: return "Invalid path";
    case ErrorCode::NotFound: return "Not found";
    case ErrorCode::PermissionDenied: return "Permission denied";
    case ErrorCode::IoError: return "Input/output error";
    case ErrorCode::ServiceUnavailable: return "Service unavailable";
    case ErrorCode::InvalidHandle: return "Invalid handle";
    case ErrorCode::InvalidTaskId: return "Invalid task ID";
    case ErrorCode::InvalidTaskState: return "Invalid task state";
    case ErrorCode::TaskCancelled: return "Task cancelled";
    case ErrorCode::TaskLimit: return "Task resource limit reached";
    case ErrorCode::UnsupportedOperation: return "Operation not supported";
    case ErrorCode::InvalidSelector: return "Invalid task result selector";
    case ErrorCode::TaskStillRunning: return "Task has not completed";
    }
    return "Unknown QL/32 error code";
}

inline ErrorCode errorCodeForMessage(std::string_view message) {
    std::string lower(message);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const auto has = [&](std::string_view part) {
        return lower.find(part) != std::string::npos;
    };
    if (has("out of memory") || has("bad_alloc")) return ErrorCode::OutOfMemory;
    if (has("execution cancelled") || has("task cancelled") ||
        has("task was killed")) return ErrorCode::TaskCancelled;
    if (has("division by zero")) return ErrorCode::DivisionByZero;
    if (has("overflow")) return ErrorCode::NumericOverflow;
    if (has("out of range") || has("out of bounds") || has("limit exceeded"))
        return ErrorCode::OutOfRange;
    if (has("type mismatch") || has("type required") || has("value required") ||
        has("must be numeric") || has("matching types")) return ErrorCode::TypeMismatch;
    if (has("no such task") || has("task id") || has("session id") ||
        has("active task") || (has("session") && has("id") &&
                                (has("no active") || has("no such"))))
        return ErrorCode::InvalidTaskId;
    if (has("task is already") || has("cannot kill") || has("logoff before") ||
        has("cannot wait") || has("already waiting") || has("attached") ||
        has("idle worker") || has("cannot be suspended") || has("not suspended"))
        return ErrorCode::InvalidTaskState;
    if (has("selector")) return ErrorCode::InvalidSelector;
    if (has("not found") || has("does not exist") || has("no such file"))
        return ErrorCode::NotFound;
    if (has("invalid path") || has("path must") || has("path escaped"))
        return ErrorCode::InvalidPath;
    if (has("channel") && (has("handle") || has("open") || has("available")))
        return ErrorCode::InvalidHandle;
    if (has("unavailable") || has("not available")) return ErrorCode::ServiceUnavailable;
    if (has("permission denied")) return ErrorCode::PermissionDenied;
    if (has("input/output") || has("i/o error") || has("read failed") ||
        has("write failed")) return ErrorCode::IoError;
    if (has("not supported") || has("unsupported"))
        return ErrorCode::UnsupportedOperation;
    if (has("syntax") || has("expected ") || has("unexpected ") ||
        has("terminator") || has("ambiguous keyword")) return ErrorCode::Syntax;
    if (has("argument") || has("requires ") || has("expects ") ||
        has("must be") || has("takes no "))
        return ErrorCode::InvalidArgument;
    return ErrorCode::GeneralFailure;
}

class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, std::string detail = {})
        : std::runtime_error(detail.empty() ? errorText(code) : detail),
          code_(code), detail_(std::move(detail)) {}

    ErrorCode code() const noexcept { return code_; }
    const std::string& detail() const noexcept { return detail_; }

private:
    ErrorCode code_;
    std::string detail_;
};

inline ErrorCode errorCodeFor(const std::exception& error) {
    return errorCodeForMessage(error.what());
}

inline std::string extendedErrorText(const std::exception& error) {
    return error.what();
}

} // namespace ql32::runtime
