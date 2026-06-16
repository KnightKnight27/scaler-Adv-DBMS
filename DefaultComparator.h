#pragma once
#include "IComparator.h"

template <typename T>
class DefaultComparator : public IComparator<T> {
public:
    int compare(const T& a, const T& b) const override {
        if (a < b) return -1;
        if (b < a) return  1;
        return 0;
    }
};