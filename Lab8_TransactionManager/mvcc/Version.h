#ifndef VERSION_H
#define VERSION_H

#include <string>

struct Version {

    int transactionId;

    std::string value;

    Version* next;

    Version(
        int txnId,
        const std::string& val
    )
        : transactionId(txnId),
          value(val),
          next(nullptr)
    {}
};

#endif