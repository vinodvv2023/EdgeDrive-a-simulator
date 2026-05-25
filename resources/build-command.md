

. Recommended Method: Using CMake (MSVC)
Since CMake handles compiler detection and environment configuration automatically, you can run the following commands in your project root:

powershell


# 1. Configure the project and generate build files in a 'build' folder
cmake -B build -S .
# 2. Build/compile the executable
cmake --build build
Output Executable: 
stt_service.exe
 (located in build/Debug/stt_service.exe).
2. Alternative Method: Raw MSVC Compiler (cl.exe)
If you want to compile the single file directly without CMake, you need to open a Developer Command Prompt for VS (which pre-configures paths to standard library headers and Windows SDKs) and run:

cmd


cl.exe /EHsc /Iexternal/cpp-httplib main.cpp /Fe:stt_service.exe
Explanation of flags:
/EHsc: Enables standard C++ exception handling.
/Iexternal/cpp-httplib: Adds the cpp-httplib directory to the header search path.
/Fe:stt_service.exe: Names the output executable file stt_service.exe.