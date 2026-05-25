#include <iostream>
#include <cuda_runtime.h>

int main() {
    int deviceCount = 0;
    cudaError_t error = cudaGetDeviceCount(&deviceCount);

    std::cout << "============================================" << std::endl;
    std::cout << "CUDA Integration Test" << std::endl;
    std::cout << "============================================" << std::endl;

    if (error != cudaSuccess) {
        std::cout << "CUDA runtime API error: " << cudaGetErrorString(error) << std::endl;
        std::cout << "CUDA is NOT fully integrated or no compatible driver is installed." << std::endl;
        return 1;
    }

    if (deviceCount == 0) {
        std::cout << "CUDA runtime loaded, but no CUDA-capable devices (GPUs) were detected." << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: CUDA is integrated and working!" << std::endl;
    std::cout << "Found " << deviceCount << " CUDA-capable device(s):" << std::endl;

    for (int i = 0; i < deviceCount; ++i) {
        cudaDeviceProp deviceProp;
        cudaError_t propError = cudaGetDeviceProperties(&deviceProp, i);
        if (propError != cudaSuccess) {
            std::cout << "  Error reading properties for device " << i << ": " << cudaGetErrorString(propError) << std::endl;
            continue;
        }
        std::cout << "\nDevice " << i << ": " << deviceProp.name << std::endl;
        std::cout << "  Compute Capability:          " << deviceProp.major << "." << deviceProp.minor << std::endl;
        std::cout << "  Total Global Memory:         " << (deviceProp.totalGlobalMem / (1024.0 * 1024.0)) << " MB" << std::endl;
        std::cout << "  Multiprocessors (SMs):       " << deviceProp.multiProcessorCount << std::endl;
        std::cout << "  Warp Size:                   " << deviceProp.warpSize << std::endl;
        std::cout << "  Max Threads per Block:       " << deviceProp.maxThreadsPerBlock << std::endl;
        std::cout << "  Max Grid Dimensions:         [" << deviceProp.maxGridSize[0] << ", " 
                  << deviceProp.maxGridSize[1] << ", " << deviceProp.maxGridSize[2] << "]" << std::endl;
        std::cout << "  Max Block Dimensions:        [" << deviceProp.maxThreadsDim[0] << ", " 
                  << deviceProp.maxThreadsDim[1] << ", " << deviceProp.maxThreadsDim[2] << "]" << std::endl;
    }
    std::cout << "============================================" << std::endl;

    return 0;
}
