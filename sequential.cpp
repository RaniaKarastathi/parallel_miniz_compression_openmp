#include <config.hpp>
#include <cmdline.hpp>
#include <utility.hpp>
#include <chrono>
#include <cstdio>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }

    // parse command line arguments and set some global variables
    long start = parseCommandLine(argc, argv);
    if (start < 0) return -1;

    auto startTime = std::chrono::high_resolution_clock::now();

    bool success = true;
    while (argv[start]) {
        size_t filesize = 0;
        if (isDirectory(argv[start], filesize)) {
            success &= walkDir(argv[start], COMP);
        } else {
            success &= doWork(argv[start], filesize, COMP);
        }
        start++;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = endTime - startTime;

    if (!success) {
        printf("Exiting with (some) Error(s)\n");
        return -1;
    }

    printf("Exiting with Success\n");
    printf("Total processing time: %.6f seconds\n", duration.count());

    return 0;
}

