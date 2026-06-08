#include "Preprocess.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: build_ben <base.csr> <query.csr> <out.ben>\n";
        return 1;
    }
    std::string base = argv[1];
    std::string query = argv[2];
    std::string ben = argv[3];
    Preprocess prep(base, query, ben);
    std::cout << "[OK] ben ready: " << ben << "\n";
    return 0;
}
