#pragma once
#include "ITree.h"
#include "IComparator.h"
#include "DefaultComparator.h"
#include "RBNode.h"
#include <memory>
#include <functional>
#include <stdexcept>

template <typename T>
class RedBlackTree : public ITree<T> {
public:
    explicit RedBlackTree(std::unique_ptr<IComparator<T>> comp = std::make_unique<DefaultComparator<T>>())
        : comparator_(std::move(comp))
    {
        nil_ = new RBNode<T>(T{}, nullptr);
        nil_->color  = Color::Black;
        nil_->left   = nil_;
        nil_->right  = nil_;
        nil_->parent = nil_;
        root_ = nil_;
    }

    ~RedBlackTree() override {
        destroySubtree(root_);
        delete nil_;
    }

    RedBlackTree(const RedBlackTree&)            = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    void insert(const T& value) override {
        RBNode<T>* node = new RBNode<T>(value, nil_);
        rbInsert(node);
    }

    void remove(const T& value) override {
        RBNode<T>* node = findNode(root_, value);
        if (node == nil_) return;
        rbDelete(node);
    }

    bool contains(const T& value) const override {
        return findNode(root_, value) != nil_;
    }

    void inorder(std::function<void(const T&)> visit) const override {
        inorderWalk(root_, visit);
    }

private:
    RBNode<T>*                      nil_;
    RBNode<T>*                      root_;
    std::unique_ptr<IComparator<T>> comparator_;

    void destroySubtree(RBNode<T>* node) {
        if (node == nil_) return;
        destroySubtree(node->left);
        destroySubtree(node->right);
        delete node;
    }

    RBNode<T>* findNode(RBNode<T>* node, const T& value) const {
        while (node != nil_) {
            int cmp = comparator_->compare(value, node->data);
            if (cmp == 0) return node;
            node = (cmp < 0) ? node->left : node->right;
        }
        return nil_;
    }

    void inorderWalk(RBNode<T>* node, std::function<void(const T&)>& visit) const {
        if (node == nil_) return;
        inorderWalk(node->left, visit);
        visit(node->data);
        inorderWalk(node->right, visit);
    }

    void rotateLeft(RBNode<T>* x) {
        RBNode<T>* y = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil_)       root_       = y;
        else if (x == x->parent->left) x->parent->left  = y;
        else                           x->parent->right = y;
        y->left   = x;
        x->parent = y;
    }

    void rotateRight(RBNode<T>* y) {
        RBNode<T>* x = y->left;
        y->left = x->right;
        if (x->right != nil_) x->right->parent = y;
        x->parent = y->parent;
        if (y->parent == nil_)       root_        = x;
        else if (y == y->parent->right) y->parent->right = x;
        else                            y->parent->left  = x;
        x->right  = y;
        y->parent = x;
    }

    void rbInsert(RBNode<T>* z) {
        RBNode<T>* y = nil_;
        RBNode<T>* x = root_;
        while (x != nil_) {
            y = x;
            int cmp = comparator_->compare(z->data, x->data);
            x = (cmp < 0) ? x->left : x->right;
        }
        z->parent = y;
        if (y == nil_)                                          root_    = z;
        else if (comparator_->compare(z->data, y->data) < 0)   y->left  = z;
        else                                                    y->right = z;
        rbInsertFixup(z);
    }

    void rbInsertFixup(RBNode<T>* z) {
        while (z->parent->color == Color::Red) {
            if (z->parent == z->parent->parent->left) {
                RBNode<T>* y = z->parent->parent->right;
                if (y->color == Color::Red) {
                    z->parent->color          = Color::Black;
                    y->color                  = Color::Black;
                    z->parent->parent->color  = Color::Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        rotateLeft(z);
                    }
                    z->parent->color         = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotateRight(z->parent->parent);
                }
            } else {
                RBNode<T>* y = z->parent->parent->left;
                if (y->color == Color::Red) {
                    z->parent->color         = Color::Black;
                    y->color                 = Color::Black;
                    z->parent->parent->color = Color::Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotateRight(z);
                    }
                    z->parent->color         = Color::Black;
                    z->parent->parent->color = Color::Red;
                    rotateLeft(z->parent->parent);
                }
            }
        }
        root_->color = Color::Black;
    }

    RBNode<T>* minimum(RBNode<T>* node) const {
        while (node->left != nil_) node = node->left;
        return node;
    }

    void rbTransplant(RBNode<T>* u, RBNode<T>* v) {
        if (u->parent == nil_)          root_             = v;
        else if (u == u->parent->left)  u->parent->left   = v;
        else                            u->parent->right  = v;
        v->parent = u->parent;
    }

    void rbDelete(RBNode<T>* z) {
        RBNode<T>* y = z;
        RBNode<T>* x;
        Color yOriginalColor = y->color;

        if (z->left == nil_) {
            x = z->right;
            rbTransplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            rbTransplant(z, z->left);
        } else {
            y = minimum(z->right);
            yOriginalColor = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                rbTransplant(y, y->right);
                y->right          = z->right;
                y->right->parent  = y;
            }
            rbTransplant(z, y);
            y->left          = z->left;
            y->left->parent  = y;
            y->color         = z->color;
        }
        delete z;
        if (yOriginalColor == Color::Black)
            rbDeleteFixup(x);
    }

    void rbDeleteFixup(RBNode<T>* x) {
        while (x != root_ && x->color == Color::Black) {
            if (x == x->parent->left) {
                RBNode<T>* w = x->parent->right;
                if (w->color == Color::Red) {
                    w->color          = Color::Black;
                    x->parent->color  = Color::Red;
                    rotateLeft(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == Color::Black && w->right->color == Color::Black) {
                    w->color = Color::Red;
                    x = x->parent;
                } else {
                    if (w->right->color == Color::Black) {
                        w->left->color = Color::Black;
                        w->color       = Color::Red;
                        rotateRight(w);
                        w = x->parent->right;
                    }
                    w->color         = x->parent->color;
                    x->parent->color = Color::Black;
                    w->right->color  = Color::Black;
                    rotateLeft(x->parent);
                    x = root_;
                }
            } else {
                RBNode<T>* w = x->parent->left;
                if (w->color == Color::Red) {
                    w->color         = Color::Black;
                    x->parent->color = Color::Red;
                    rotateRight(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == Color::Black && w->left->color == Color::Black) {
                    w->color = Color::Red;
                    x = x->parent;
                } else {
                    if (w->left->color == Color::Black) {
                        w->right->color = Color::Black;
                        w->color        = Color::Red;
                        rotateLeft(w);
                        w = x->parent->left;
                    }
                    w->color         = x->parent->color;
                    x->parent->color = Color::Black;
                    w->left->color   = Color::Black;
                    rotateRight(x->parent);
                    x = root_;
                }
            }
        }
        x->color = Color::Black;
    }
};