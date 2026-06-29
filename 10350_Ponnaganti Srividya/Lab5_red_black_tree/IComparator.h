#pragma once

template <typename T>
class IComparator {
public:
    virtual ~IComparator() = default;
    virtual int compare(const T& a, const T& b) const = 0;
};