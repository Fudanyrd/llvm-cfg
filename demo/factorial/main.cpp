
#include <algorithm>
#include <cstdlib>
#include <iostream>

extern "C" int factorial(int n);

int main(int argc, char **argv) {
    for (int i = 1; i < std::max(3, argc); i++) {
        int fb = factorial(atoi(argv[i]));
        std::cout << fb << std::endl;
    }

    return 0;
}
