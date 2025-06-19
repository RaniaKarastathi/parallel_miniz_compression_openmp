# Parallel_miniz_compression_openmp

Overview
Parallel compressor/decompressor using the Miniz library (DEFLATE algorithm) and OpenMP. Handles multiple small files and large files by splitting them into blocks while preserving compression context for correct decompression.

Features
Sequential and parallel C++17 implementations.
Efficient compression of both small and large files.
Performance tested on SPM cluster nodes.
Includes source files and Makefile

Files included
miniz/: Miniz library (provided)
cmdline.cpp, config.cpp, utility.cpp (provided)
sequential.cpp, parallel.cpp: Source code
Makefile: Build and run
