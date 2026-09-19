#include <string>

#include "test_framework.hpp"

int main(int argc, char* argv[]) {
    // One optional argument: a substring filter on "suite.name".
    const std::string filter = (argc > 1) ? argv[1] : "";
    return testing::runAll(filter);
}
