// RAIIHandle.h

#pragma once

#include <windows.h>
#include <stdexcept>
#include <system_error>
#include <string>

/**
 * @brief RAII class for generic HANDLEs (e.g., mutexes, files).
 *
 * Manages the lifetime of a Windows HANDLE, ensuring it is properly closed.
 */
class RAIIHandle {
public:
    /**
     * @brief Constructs a RAIIHandle with the given HANDLE.
     * @param handle The HANDLE to manage.
     * @throws std::system_error if the HANDLE is invalid.
     */
    explicit RAIIHandle(HANDLE handle = nullptr) : handle_(handle) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::system_error(GetLastError(), std::system_category(), "Invalid HANDLE provided to RAIIHandle");
        }
    }

    /**
     * @brief Destructor that closes the HANDLE if it's valid.
     */
    ~RAIIHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    // Disable copy semantics
    RAIIHandle(const RAIIHandle&) = delete;
    RAIIHandle& operator=(const RAIIHandle&) = delete;

    // Enable move semantics
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

    /**
     * @brief Retrieves the managed HANDLE.
     * @return The managed HANDLE.
     */
    HANDLE get() const { return handle_; }

    /**
     * @brief Releases ownership of the HANDLE without closing it.
     * @return The managed HANDLE.
     */
    HANDLE release() {
        HANDLE temp = handle_;
        handle_ = nullptr;
        return temp;
    }

private:
    HANDLE handle_;
};

/**
 * @brief RAII class specifically for HMODULEs.
 *
 * Manages the lifetime of a Windows HMODULE, ensuring it is properly freed.
 */
class RAIIHMODULE {
public:
    /**
     * @brief Constructs a RAIIHMODULE with the given HMODULE.
     * @param hModule The HMODULE to manage.
     * @throws std::system_error if the HMODULE is invalid.
     */
    explicit RAIIHMODULE(HMODULE hModule = nullptr) : hModule_(hModule) {
        if (hModule_ == INVALID_HANDLE_VALUE) {
            throw std::system_error(GetLastError(), std::system_category(), "Invalid HMODULE provided to RAIIHMODULE");
        }
    }

    /**
     * @brief Destructor that frees the HMODULE if it's valid.
     */
    ~RAIIHMODULE() {
        if (hModule_) {
            FreeLibrary(hModule_);
        }
    }

    // Disable copy semantics
    RAIIHMODULE(const RAIIHMODULE&) = delete;
    RAIIHMODULE& operator=(const RAIIHMODULE&) = delete;

    // Enable move semantics
    RAIIHMODULE(RAIIHMODULE&& other) noexcept : hModule_(other.hModule_) {
        other.hModule_ = nullptr;
    }

    RAIIHMODULE& operator=(RAIIHMODULE&& other) noexcept {
        if (this != &other) {
            if (hModule_) {
                FreeLibrary(hModule_);
            }
            hModule_ = other.hModule_;
            other.hModule_ = nullptr;
        }
        return *this;
    }

    /**
     * @brief Retrieves the managed HMODULE.
     * @return The managed HMODULE.
     */
    HMODULE get() const { return hModule_; }

    /**
     * @brief Releases ownership of the HMODULE without freeing it.
     * @return The managed HMODULE.
     */
    HMODULE release() {
        HMODULE temp = hModule_;
        hModule_ = nullptr;
        return temp;
    }

private:
    HMODULE hModule_;
};
