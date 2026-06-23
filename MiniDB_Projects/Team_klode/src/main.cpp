#include "minidb.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string dataDir = "data";
    bool runDemo = false;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--demo") runDemo = true;
        else dataDir = arg;
    }
    if (argc > 2 && std::string(argv[2]) == "--demo") runDemo = true;

    try {
        minidb::MiniDB db(dataDir);
        if (runDemo) {
            std::cout << minidb::renderResult(db.demo());
            return 0;
        }

        std::cout << "MiniDB Team klode. Enter SQL; type exit to quit.\n";
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "exit" || line == "quit") break;
            try {
                std::cout << minidb::renderResult(db.execute(line));
            } catch (const std::exception& ex) {
                std::cout << "error: " << ex.what() << "\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
