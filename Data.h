#pragma once

// holds the value and how many times it was recently used
template <class ValueT>
struct Data {
    ValueT value;
    int frequency;
};