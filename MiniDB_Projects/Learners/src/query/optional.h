#ifndef CUSTOM_OPTIONAL_H
#define CUSTOM_OPTIONAL_H

#include <stdexcept>

template<typename T>
class Optional {
private:
    bool has_val;
    T val;

public:
    Optional() : has_val(false), val() {}
    Optional(const T& v) : has_val(true), val(v) {}
    
    bool has_value() const { return has_val; }
    const T& value() const { 
        if (!has_val) throw std::runtime_error("Optional has no value");
        return val; 
    }
    T& value() { 
        if (!has_val) throw std::runtime_error("Optional has no value");
        return val; 
    }
    
    const T& operator*() const { return val; }
    T& operator*() { return val; }
    
    const T* operator->() const { return &val; }
    T* operator->() { return &val; }
    
    explicit operator bool() const { return has_val; }
    
    void reset() {
        has_val = false;
        val = T();
    }
};

#endif
