#!/bin/bash

if [ -f "hedgedcurl.cpp" ]; then
    echo "Компиляция C++ hedgedcurl..."
    "/mnt/d/CLion 2026.1.3/bin/cmake/win/x64/bin/cmake.exe" --build cmake-build-debug --target hedgedcurl || exit 1
elif [ -f "hedgedcurl.go" ]; then
    echo "Компиляция Go hedgedcurl..."
    go build -o hedgedcurl hedgedcurl.go
elif [ -f "hedgedcurl.py" ]; then
    echo "Python не требует компиляции"
    exit 0
elif [ -f "hedgedcurl.java" ]; then
    echo "Компиляция Java hedgedcurl..."
    javac hedgedcurl.java
else
    echo "Не найден файл hedgedcurl для компиляции"
    echo "Поддерживаемые файлы: hedgedcurl.{cpp,go,py,java}"
    exit 1
fi

echo "Компиляция hedgedcurl завершена"