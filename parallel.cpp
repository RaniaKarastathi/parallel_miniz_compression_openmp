#include <config.hpp>
#include <cmdline.hpp>
#include <utility.hpp>
#include <chrono>
#include <cstdio>
#include <omp.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

size_t BLOCK_SIZE = 1 * 1024 * 1024;

// Compress/Decompress a large file by dividing it into blocks and compressing them in parallel
bool LargeFileParallel(const char* filename, bool comp) {
  if (comp) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        printf("Error opening large file: %s\n", filename);
        return false;
    }

    std::streamsize size = file.tellg();           // Get file size
    file.seekg(0, std::ios::beg);                  // Reset read pointer to the beginning

    std::vector<unsigned char> fullData(size);
    file.read(reinterpret_cast<char*>(fullData.data()), size);
    file.close();

    int numBlocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;  // Calculate number of blocks

    // Prepare containers for each block's compressed data and sizes
    std::vector<std::vector<unsigned char>> compressedBlocks(numBlocks);
    std::vector<size_t> blockSizes(numBlocks);

    // Parallelize the compression of each block using dynamic scheduling
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < numBlocks; i++) {
        size_t offset = i * BLOCK_SIZE;
        size_t toRead = std::min(BLOCK_SIZE, (size_t)std::max((std::streamsize)0, size - (std::streamsize)offset));
        std::vector<unsigned char> input(fullData.begin() + offset, fullData.begin() + offset + toRead);

         // Compress the data
         size_t compressedSize = compressBound(toRead);
         compressedBlocks[i].resize(compressedSize);
         int res = compress(compressedBlocks[i].data(), &compressedSize, input.data(), toRead);

        if (res != Z_OK) {
            printf("Compression failed at block %d\n", i);
            continue;
        }
        blockSizes[i] = compressedSize;
        compressedBlocks[i].resize(compressedSize); // Resize buffer to actual compressed size
    }
     // Write compressed blocks to output file along with their sizes
     std::string outFilename = std::string(filename) + ".zip";
     std::ofstream out(outFilename, std::ios::binary);
 
     if (!out) {
         std::cerr << "Error creating output file: " << outFilename << std::endl;
         return false;
     }
 
     for (int i = 0; i < numBlocks; i++) {
         size_t sz = blockSizes[i];
         out.write(reinterpret_cast<const char*>(&sz), sizeof(size_t));
         out.write(reinterpret_cast<const char*>(compressedBlocks[i].data()), sz);
     }
     out.close();
      if (remove(filename) != 0) {
            perror("remove");
      }
   } else {
         std::ifstream in(filename, std::ios::binary);
         if (!in) {
             std::cerr << "Error opening compressed file: " << filename << std::endl;
             return false;
         }
 
         std::vector<std::vector<unsigned char>> compressedBlocks;
         std::vector<size_t> blockSizes;
 
         while (in.peek() != EOF) {
             size_t sz;
             in.read(reinterpret_cast<char*>(&sz), sizeof(size_t));
             std::vector<unsigned char> block(sz);
             in.read(reinterpret_cast<char*>(block.data()), sz);
             compressedBlocks.push_back(block);
             blockSizes.push_back(sz);
         }
         in.close();
 
         int numBlocks = compressedBlocks.size();
         std::vector<std::vector<unsigned char>> decompressedBlocks(numBlocks);
         #pragma omp parallel for schedule(dynamic)
         for (int i = 0; i < numBlocks; i++) {
             std::vector<unsigned char> &compressed = compressedBlocks[i];
             std::vector<unsigned char> decompressed(BLOCK_SIZE);
             uLongf destLen = BLOCK_SIZE;
 
             int res = uncompress(decompressed.data(), &destLen, compressed.data(), blockSizes[i]);
             if (res != Z_OK) {
                 continue;
             }
             decompressed.resize(destLen);
             decompressedBlocks[i] = decompressed;
         }
 
         std::string outFilename = std::string(filename);
         size_t dot = outFilename.rfind(".zip");
         if (dot != std::string::npos)
             outFilename = outFilename.substr(0, dot);
         else
             outFilename += ".out";
 
         std::ofstream out(outFilename, std::ios::binary);
         if (!out) {
             std::cerr << "Error writing decompressed file: " << outFilename << std::endl;
             return false;
         }
 
         for (auto& block : decompressedBlocks)
             out.write(reinterpret_cast<const char*>(block.data()), block.size());
 
         out.close();
         if (remove(filename) != 0) {
            perror("remove");
        }

     }
     return true;
 }

int main(int argc, char *argv[]) {
    omp_set_nested(1);
    if (argc < 2) {
        usage(argv[0]);  // Show usage if insufficient arguments
        return -1;
    }

    long start = parseCommandLine(argc, argv); // Parse command-line arguments
    if (start < 0) return -1;

    auto startTime = std::chrono::high_resolution_clock::now();

    bool success = true;
 // Parallel region with OpenMP. Each file or directory becomes a task.
 #pragma omp parallel
 {
     #pragma omp single nowait
     {
         while (argv[start]) {
             size_t filesize = 0;
             // If argument is a directory
             if (isDirectory(argv[start], filesize)) {
                DIR *dir = opendir(argv[start]);
                if (dir == NULL) {
                    perror("opendir");
                    success = false;
                } else {
                    struct dirent *entry;
                    while ((entry = readdir(dir)) != NULL) {
                        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                            continue;

                        std::string fullPath = std::string(argv[start]) + "/" + entry->d_name;

                        struct stat statbuf;
                        if (stat(fullPath.c_str(), &statbuf) == -1) {
                            perror("stat");
                            success = false;
                            continue;
                        }

                        #pragma omp task shared(success)
                        {
                            if (S_ISDIR(statbuf.st_mode)) {
                                success &= walkDir(fullPath.c_str(), COMP);  
                            } else {
                                if (statbuf.st_size > 16 * 1024 * 1024)
                                    success &= LargeFileParallel(fullPath.c_str(),COMP);
                                else
                                    success &= doWork(fullPath.c_str(), statbuf.st_size, COMP);
                            }
                        }
                    }
                    closedir(dir);
                }

             }
             // If the file is large (>16MB), split and compress in parallel
             else {
                 // Compute filesize for non-directory files
                 std::ifstream file(argv[start], std::ios::binary | std::ios::ate);
                 if (file) {
                     filesize = file.tellg();  // Get the file size
                 } else {
                     printf("Error opening file: %s\n", argv[start]);
                     continue;
                 }

                 // If file is larger than 16MB, use parallel compression
                 if (filesize > 16 * 1024 * 1024) {
                     #pragma omp task shared(success)
                     {  
                         success &= LargeFileParallel(argv[start],COMP);
                     }
                 }
                 // Otherwise, handle small files sequentially
                 else {
                     #pragma omp task shared(success)
                     {
                         success &= doWork(argv[start], filesize, COMP);
                     }
                 }
             }

             start++;
         }
         
            // Ensure all tasks are completed before moving forward
            #pragma omp taskwait
        }
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
