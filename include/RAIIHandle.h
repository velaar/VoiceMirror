// RAIIHandle.h
#pragma once
#include <windows.h>

/**
 * @brief A RAII wrapper for Windows HANDLEs.
 *
 * Ensures that HANDLEs are properly closed when the RAIIHandle object goes out of scope.
 */
class RAIIHandle {
public:
    // Constructor initializes the handle
    explicit RAIIHandle(HANDLE handle = nullptr) : handle_(handle) {}

    // Destructor ensures the handle is closed
    ~RAIIHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    // Delete copy constructor and copy assignment to prevent copying
    RAIIHandle(const RAIIHandle&) = delete;
    RAIIHandle& operator=(const RAIIHandle&) = delete;

    // Move constructor and move assignment to allow moving
    RAIIHandle(RAIIHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    RAIIHandle& operator=(RAIIHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Accessor for the handle
    HANDLE get() const { return handle_; }

    // Accessor to get the address of the handle (useful for functions like CreateMutex)
    HANDLE* getAddressOf() { return &handle_; }

private:
    HANDLE handle_;
};
