# Build instructions (C++)

This project includes two C++ programs:

- `storage_buffer/main.cpp` — ClockSweep demo
- `lab3/24bcs10276/code.cpp` — ClockSweep demo

Recommended: use CMake and a C++17-capable compiler.

Linux / WSL / macOS

```bash
sudo apt update && sudo apt install -y build-essential cmake   # Debian/Ubuntu
mkdir build && cd build
cmake ..
cmake --build . -- -j$(nproc)
# Run binaries:
./storage_buffer/storage_buffer_main
./lab3/24bcs10276/lab3_code
```

Windows (PowerShell) with MSVC (Developer Command Prompt recommended)

```powershell
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
.\Release\storage_buffer_main.exe
.\Release\lab3_code.exe
```

CI
The repository contains a GitHub Actions workflow `.github/workflows/cpp-ci.yml` that builds the project on Ubuntu for each push/PR.
