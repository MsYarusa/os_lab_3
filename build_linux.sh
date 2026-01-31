#!/bin/bash

echo "Updating from Git..."
git pull origin master

echo "Configuring CMake..."
cmake -S . -B build

echo "Building project..."
cmake --build build

echo "Done! Executable is in build/lab_3"