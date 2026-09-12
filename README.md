[Read in English](README.md) | [Читать на русском](README.ru.md)

![Logotype](images/webgpu_logo.png)

# WebGPU Examples (C++ / Dawn)

This repository contains a collection of educational examples for working with the WebGPU graphics API in C++.

All examples are drawn from my personal experience learning graphics APIs. I have tried to structure the learning curve from the most basic concepts (window initialization) to more advanced ones.

The main principle of the repository is simplicity and focus. Reusable boilerplate code from previous examples is encapsulated into a single header and source file pair (.h/.cpp). Meanwhile, all the new WebGPU logic for the current example is kept entirely within the main file. The architecture is intentionally kept flat so you can easily read the core logic from top to bottom, accompanied by detailed explanatory comments.

## Project Structure

The repository is divided into folders (levels). Each folder contains its own independent `CMakeLists.txt` and the example's source code:

- `01_hello_window/` — Basic initialization of the instance, adapter, device, and window.
    
- `02_hello_triangle/` — ... (and so on)
    

## Build Requirements

To successfully compile the examples on your computer, you must have the following installed:

1. **CMake** (version 3.20 or higher).
    
2. **C++ Compiler** with C++20 standard support.
    
3. **GLFW** — used for creating cross-platform windows and handling events.
    
4. **Google Dawn** (WebGPU implementation).
    
    - **Important:** The project assumes that you have previously built and installed the Dawn library. Official instructions for building Dawn via CMake are available [here](https://github.com/google/dawn/blob/main/docs/quickstart-cmake.md).
        

## How to Build the Project

Clone the repository and use standard CMake commands (for example, building via Ninja or MSBuild):

```
cmake -S . -B build
cmake --build build
```

## Useful Resources

* [Official google-dawn repository with examples](https://github.com/google/dawn)
* [The best start for a beginner](https://github.com/eliemichel/LearnWebGPU)