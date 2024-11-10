#ifndef RAIIHANDLE_H
#define RAIIHANDLE_H

#include <windows.h>
#include <stdexcept>
#include <system_error>
#include <string>
#include "Logger.h"

// RAII class for generic HANDLEs (e.g., mutexes, files)
class RAIIHandle {
public:
    explicit RAIIHandle(HANDLE handle = nullptr) : handle_(handle) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            LOG_ERROR("RAIIHandle received INVALID_HANDLE_VALUE.");
            throw std::system_error(GetLastError(), std::system_category(), "Invalid HANDLE provided to RAIIHandle");
        }
        if (handle_ != nullptr) {
            LOG_DEBUG("RAIIHandle constructed with HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(handle_)));
        } else {
            LOG_DEBUG("RAIIHandle constructed with nullptr.");
        }
    }

    ~RAIIHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            LOG_DEBUG("RAIIHandle destructing and closing HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(handle_)));
            CloseHandle(handle_);
        }
    }

    // Disable copy semantics
    RAIIHandle(const RAIIHandle&) = delete;
    RAIIHandle& operator=(const RAIIHandle&) = delete;

    // Enable move semantics
    RAIIHandle(RAIIHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
        LOG_DEBUG("RAIIHandle move constructed. New HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(handle_)));
    }

    RAIIHandle& operator=(RAIIHandle&& other) noexcept {
        if (this != &other) {
            if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
                LOG_DEBUG("RAIIHandle move assigned. Closing existing HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(handle_)));
                CloseHandle(handle_);
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
            LOG_DEBUG("RAIIHandle move assigned. New HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(handle_)));
        }
        return *this;
    }

    HANDLE get() const { return handle_; }

    // Optionally, provide a release method
    HANDLE release() {
        HANDLE temp = handle_;
        handle_ = nullptr;
        LOG_DEBUG("RAIIHandle released HANDLE: " + std::to_string(reinterpret_cast<std::uintptr_t>(temp)));
        return temp;
    }

private:
    HANDLE handle_;
};

// RAII class specifically for HMODULEs
class RAIIHMODULE {
public:
    explicit RAIIHMODULE(HMODULE hModule = nullptr) : hModule_(hModule) {
        if (hModule_ == INVALID_HANDLE_VALUE) {
            LOG_ERROR("RAIIHMODULE received INVALID_HANDLE_VALUE.");
            throw std::system_error(GetLastError(), std::system_category(), "Invalid HMODULE provided to RAIIHMODULE");
        }
        if (hModule_ != nullptr) {
            LOG_DEBUG("RAIIHMODULE constructed with HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(hModule_)));
        } else {
            LOG_DEBUG("RAIIHMODULE constructed with nullptr.");
        }
    }

    ~RAIIHMODULE() {
        if (hModule_) {
            LOG_DEBUG("RAIIHMODULE destructing and freeing HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(hModule_)));
            FreeLibrary(hModule_);
        }
    }

    // Disable copy semantics
    RAIIHMODULE(const RAIIHMODULE&) = delete;
    RAIIHMODULE& operator=(const RAIIHMODULE&) = delete;

    // Enable move semantics
    RAIIHMODULE(RAIIHMODULE&& other) noexcept : hModule_(other.hModule_) {
        other.hModule_ = nullptr;
        LOG_DEBUG("RAIIHMODULE move constructed. New HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(hModule_)));
    }

    RAIIHMODULE& operator=(RAIIHMODULE&& other) noexcept {
        if (this != &other) {
            if (hModule_) {
                LOG_DEBUG("RAIIHMODULE move assigned. Freeing existing HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(hModule_)));
                FreeLibrary(hModule_);
            }
            hModule_ = other.hModule_;
            other.hModule_ = nullptr;
            LOG_DEBUG("RAIIHMODULE move assigned. New HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(hModule_)));
        }
        return *this;
    }

    HMODULE get() const { return hModule_; }

    // Optionally, provide a release method
    HMODULE release() {
        HMODULE temp = hModule_;
        hModule_ = nullptr;
        LOG_DEBUG("RAIIHMODULE released HMODULE: " + std::to_string(reinterpret_cast<std::uintptr_t>(temp)));
        return temp;
    }

private:
    HMODULE hModule_;
};

#endif // RAIIHANDLE_H
