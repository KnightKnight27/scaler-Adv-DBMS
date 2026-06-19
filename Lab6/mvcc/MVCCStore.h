#ifndef MVCCSTORE_H
#define MVCCSTORE_H

#include <iostream>
#include <unordered_map>
#include <string>

#include "Version.h"

class MVCCStore {
private:

    std::unordered_map<int, Version*> records;

public:

    void write(
        int key,
        int transactionId,
        const std::string& value
    ) {

        Version* version =
            new Version(
                transactionId,
                value
            );

        version->next =
            records[key];

        records[key] =
            version;
    }

    std::string read(
        int key
    ) {

        if (records.find(key) ==
            records.end())
            return "";

        return records[key]->value;
    }

    void printVersions(
        int key
    ) {

        if (records.find(key) ==
            records.end()) {

            std::cout
                << "No versions found\n";

            return;
        }

        Version* current =
            records[key];

        std::cout
            << "Version Chain for Key "
            << key
            << ":\n";

        while (current != nullptr) {

            std::cout
                << "[T"
                << current->transactionId
                << ": "
                << current->value
                << "]";

            if (current->next)
                std::cout << " -> ";

            current =
                current->next;
        }

        std::cout
            << std::endl;
    }

    ~MVCCStore() {

        for (auto& pair : records) {

            Version* current =
                pair.second;

            while (current) {

                Version* next =
                    current->next;

                delete current;

                current = next;
            }
        }
    }
};

#endif