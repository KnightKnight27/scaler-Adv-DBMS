#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

class StudentBTree {
private:
    struct Page {
        bool leaf;
        std::vector<int> values;
        std::vector<Page*> branches;

        explicit Page(bool leafNode) : leaf(leafNode) {}

        ~Page() {
            for (Page* child : branches) {
                delete child;
            }
        }
    };

    Page* root_;
    int minimumDegree_;

    int maxValuesPerPage() const {
        return 2 * minimumDegree_ - 1;
    }

    int minValuesPerNonRootPage() const {
        return minimumDegree_ - 1;
    }

    bool locateValue(const Page* page, int value) const {
        if (page == nullptr) {
            return false;
        }

        int index = 0;
        while (index < static_cast<int>(page->values.size()) &&
               value > page->values[index]) {
            index++;
        }

        if (index < static_cast<int>(page->values.size()) &&
            page->values[index] == value) {
            return true;
        }

        if (page->leaf) {
            return false;
        }

        return locateValue(page->branches[index], value);
    }

    void divideChildNode(Page* parent, int childIndex) {
        Page* leftPart = parent->branches[childIndex];
        Page* rightPart = new Page(leftPart->leaf);

        const int middleValue = leftPart->values[minimumDegree_ - 1];

        rightPart->values.assign(leftPart->values.begin() + minimumDegree_,
                                 leftPart->values.end());

        leftPart->values.resize(minimumDegree_ - 1);

        if (!leftPart->leaf) {
            rightPart->branches.assign(leftPart->branches.begin() + minimumDegree_,
                                       leftPart->branches.end());

            leftPart->branches.resize(minimumDegree_);
        }

        parent->values.insert(parent->values.begin() + childIndex, middleValue);
        parent->branches.insert(parent->branches.begin() + childIndex + 1,
                                rightPart);
    }

    void addToAvailableNode(Page* page, int value) {
        int index = static_cast<int>(page->values.size()) - 1;

        if (page->leaf) {
            page->values.push_back(value);

            while (index >= 0 && page->values[index] > value) {
                page->values[index + 1] = page->values[index];
                index--;
            }

            page->values[index + 1] = value;
            return;
        }

        while (index >= 0 && value < page->values[index]) {
            index--;
        }

        index++;

        if (static_cast<int>(page->branches[index]->values.size()) ==
            maxValuesPerPage()) {
            divideChildNode(page, index);

            if (value > page->values[index]) {
                index++;
            }
        }

        addToAvailableNode(page->branches[index], value);
    }

    void gatherSortedValues(const Page* page, std::vector<int>& ordered) const {
        for (std::size_t i = 0; i < page->values.size(); i++) {
            if (!page->leaf) {
                gatherSortedValues(page->branches[i], ordered);
            }

            ordered.push_back(page->values[i]);
        }

        if (!page->leaf) {
            gatherSortedValues(page->branches.back(), ordered);
        }
    }

    bool validateNodeRules(const Page* page,
                           bool isRoot,
                           int depth,
                           int& leafDepth,
                           long long lowerBound,
                           long long upperBound,
                           std::string& issue) const {
        if (page == nullptr) {
            issue = "Null child pointer found inside the tree.";
            return false;
        }

        const int keyCount = static_cast<int>(page->values.size());

        if (keyCount == 0) {
            issue = isRoot ? "Root page has no keys."
                           : "A non-root page has no keys.";
            return false;
        }

        if (keyCount > maxValuesPerPage()) {
            issue = "A page has more than 2t - 1 keys.";
            return false;
        }

        if (!isRoot && keyCount < minValuesPerNonRootPage()) {
            issue = "A non-root page has fewer than t - 1 keys.";
            return false;
        }

        for (int i = 0; i < keyCount; i++) {
            if (page->values[i] <= lowerBound || page->values[i] >= upperBound) {
                issue = "A key is outside the allowed range for its page.";
                return false;
            }

            if (i > 0 && page->values[i] <= page->values[i - 1]) {
                issue = "Keys inside one page are not strictly increasing.";
                return false;
            }
        }

        if (page->leaf) {
            if (!page->branches.empty()) {
                issue = "A leaf page should not contain child pointers.";
                return false;
            }

            if (leafDepth == -1) {
                leafDepth = depth;
            } else if (leafDepth != depth) {
                issue = "Leaves are not all at the same depth.";
                return false;
            }

            return true;
        }

        if (static_cast<int>(page->branches.size()) != keyCount + 1) {
            issue = "An internal page does not have key_count + 1 children.";
            return false;
        }

        for (int i = 0; i <= keyCount; i++) {
            const long long nextLower =
                (i == 0) ? lowerBound : page->values[i - 1];

            const long long nextUpper =
                (i == keyCount) ? upperBound : page->values[i];

            if (!validateNodeRules(page->branches[i],
                                   false,
                                   depth + 1,
                                   leafDepth,
                                   nextLower,
                                   nextUpper,
                                   issue)) {
                return false;
            }
        }

        return true;
    }

public:
    explicit StudentBTree(int degree)
        : root_(nullptr), minimumDegree_(degree < 2 ? 2 : degree) {}

    ~StudentBTree() {
        delete root_;
    }

    StudentBTree(const StudentBTree&) = delete;
    StudentBTree& operator=(const StudentBTree&) = delete;

    bool insert(int value) {
        if (root_ == nullptr) {
            root_ = new Page(true);
            root_->values.push_back(value);
            return true;
        }

        if (search(value)) {
            return false;
        }

        if (static_cast<int>(root_->values.size()) == maxValuesPerPage()) {
            Page* freshRoot = new Page(false);
            freshRoot->branches.push_back(root_);
            divideChildNode(freshRoot, 0);
            root_ = freshRoot;
        }

        addToAvailableNode(root_, value);
        return true;
    }

    bool search(int value) const {
        return locateValue(root_, value);
    }

    void printSorted(std::ostream& out) const {
        if (root_ == nullptr) {
            out << "(tree is empty)\n";
            return;
        }

        std::vector<int> ordered;
        gatherSortedValues(root_, ordered);

        for (std::size_t i = 0; i < ordered.size(); i++) {
            if (i > 0) {
                out << ' ';
            }

            out << ordered[i];
        }

        out << '\n';
    }

    void showLevels(std::ostream& out) const {
        if (root_ == nullptr) {
            out << "(tree is empty)\n";
            return;
        }

        std::queue<const Page*> pending;
        pending.push(root_);

        int level = 0;

        while (!pending.empty()) {
            const int levelCount = static_cast<int>(pending.size());

            out << "Layer " << level << ": ";

            for (int i = 0; i < levelCount; i++) {
                const Page* current = pending.front();
                pending.pop();

                out << "<";

                for (std::size_t j = 0; j < current->values.size(); j++) {
                    if (j > 0) {
                        out << ", ";
                    }

                    out << current->values[j];
                }

                out << ">";

                if (i + 1 < levelCount) {
                    out << "   ";
                }

                if (!current->leaf) {
                    for (const Page* child : current->branches) {
                        pending.push(child);
                    }
                }
            }

            out << '\n';
            level++;
        }
    }

    bool validate(std::string& issue) const {
        issue.clear();

        if (root_ == nullptr) {
            return true;
        }

        int leafDepth = -1;

        return validateNodeRules(root_,
                                 true,
                                 0,
                                 leafDepth,
                                 std::numeric_limits<long long>::lowest(),
                                 std::numeric_limits<long long>::max(),
                                 issue);
    }
};

void printValidationLine(const StudentBTree& tree) {
    std::string issue;
    const bool ok = tree.validate(issue);

    std::cout << "Validation status: " << (ok ? "passed" : "failed");

    if (!ok) {
        std::cout << " (" << issue << ")";
    }

    std::cout << '\n';
}

void printSearchLine(const StudentBTree& tree, int value) {
    std::cout << "Search " << value << ": "
              << (tree.search(value) ? "present" : "absent") << '\n';
}

void presentMixedCase() {
    std::cout << "\nCase 1: Degree 3 with mixed insertions\n";

    StudentBTree tree(3);

    const int sampleValues[] = {
        42, 7, 19, 63, 12, 25, 50,
        70, 5, 17, 29, 33, 46, 54
    };

    for (int value : sampleValues) {
        tree.insert(value);
    }

    std::cout << "Level view:\n";
    tree.showLevels(std::cout);

    std::cout << "Sorted walk: ";
    tree.printSorted(std::cout);

    printSearchLine(tree, 29);
    printSearchLine(tree, 100);
    printValidationLine(tree);
}

void presentSequentialCase() {
    std::cout << "\nCase 2: Degree 2 with values 5 to 32\n";

    StudentBTree tree(2);

    for (int value = 5; value <= 32; value++) {
        tree.insert(value);
    }

    std::cout << "Level view:\n";
    tree.showLevels(std::cout);

    std::cout << "Sorted walk: ";
    tree.printSorted(std::cout);

    printValidationLine(tree);
}

void presentVolumeCase() {
    std::cout << "\nCase 3: Degree 30 with 5000 keys\n";

    StudentBTree tree(30);

    for (int i = 0; i < 5000; i++) {
        tree.insert(i * 2 + 3);
    }

    printValidationLine(tree);

    printSearchLine(tree, 3);
    printSearchLine(tree, 5001);
    printSearchLine(tree, 10001);
    printSearchLine(tree, 4);
    printSearchLine(tree, 12000);
}

int main() {
    std::cout << "B-Tree lab demonstration\n";
    std::cout << "------------------------\n";

    presentMixedCase();
    presentSequentialCase();
    presentVolumeCase();

    return 0;
}