#pragma once
#include <functional>

template <typename T>
class ITree {
public:
    virtual ~ITree() = default;
    virtual void insert(const T& value) = 0;
    virtual void remove(const T& value) = 0;
    virtual bool contains(const T& value) const = 0;
    virtual void inorder(std::function<void(const T&)> visit) const = 0;
};