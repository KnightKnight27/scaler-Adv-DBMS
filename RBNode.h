#pragma once

enum class Color { Red, Black };

template <typename T>
struct RBNode {
    T     data;
    Color color;
    RBNode<T>* parent;
    RBNode<T>* left;
    RBNode<T>* right;

    explicit RBNode(const T& val, RBNode<T>* nil)
        : data(val), color(Color::Red), parent(nil), left(nil), right(nil) {}
};






