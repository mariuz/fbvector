#pragma once

#ifndef FB_UDR_STATUS_TYPE
#define FB_UDR_STATUS_TYPE ::Firebird::ThrowStatusWrapper
#endif

#include "ibase.h"
#include "firebird/UdrCppEngine.h"

namespace fbvector {

// Raises a custom error in Firebird database engine.
void raise_error(Firebird::ThrowStatusWrapper* status, const char* message);

// RAII wrapper for Firebird API interfaces that must be released using release().
template <typename T>
class AutoRelease {
public:
    AutoRelease(T* p = nullptr) : ptr(p) {}
    ~AutoRelease() {
        if (ptr) {
            ptr->release();
        }
    }
    
    AutoRelease(const AutoRelease&) = delete;
    AutoRelease& operator=(const AutoRelease&) = delete;
    
    AutoRelease(AutoRelease&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }
    
    AutoRelease& operator=(AutoRelease&& other) noexcept {
        if (this != &other) {
            if (ptr) ptr->release();
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    
    T* operator->() const { return ptr; }
    operator T*() const { return ptr; }
    T* get() const { return ptr; }
    
    T** operator&() {
        if (ptr) {
            ptr->release();
            ptr = nullptr;
        }
        return &ptr;
    }
    
    void reset(T* p = nullptr) {
        if (ptr != p) {
            if (ptr) ptr->release();
            ptr = p;
        }
    }
    
    T* release() {
        T* temp = ptr;
        ptr = nullptr;
        return temp;
    }
    
private:
    T* ptr;
};

} // namespace fbvector
