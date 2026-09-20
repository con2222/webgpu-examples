[Read in English](README.md) | [Читать на русском](README.ru.md)

![Logotype](images/webgpu_logo.png)

# WebGPU Examples (C++ / Dawn)

A collection of small educational examples for learning **WebGPU in C++ using Google Dawn**.

The examples follow my own journey into graphics programming, starting with a triangle and gradually introducing textures, instancing, compute shaders, post-processing, and storage textures.

The main principle is **simplicity and focus**: each example introduces a specific concept and keeps the relevant WebGPU code visible.

## Learning Approach

The examples are arranged in a suggested learning order. Later examples build on concepts introduced earlier, but each produces a separate executable.

In most examples, familiar initialization and window management code lives in a local `context.hpp` / `context.cpp` pair. The code for the current topic stays in the example's main source file, alongside its WGSL shaders and explanatory comments.

A useful way to work through the repository:

1. Build and run an example.
2. Read the code and identify the new concept.
3. Change a parameter, shader, or resource configuration.
4. Predict the result, then compare it with what actually happens.

## Examples

All example directories are located inside [`examples/`](examples/).

| Directory                                                                  | Main concepts                                                                                            |
| -------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| [`01_00_hardcode_triangle`](examples/01_00_hardcode_triangle/)             | Instance, adapter, device, surface, render pipeline, and a triangle defined in the vertex shader.        |
| [`02_00_vertex_buffer_triangle`](examples/02_00_vertex_buffer_triangle/)   | Vertex buffers, vertex attributes, and interleaved vertex data.                                          |
| [`03_00_indexed_rectangle`](examples/03_00_indexed_rectangle/)             | Index buffers and indexed drawing.                                                                       |
| [`04_00_planar_triangle`](examples/04_00_planar_triangle/)                 | Separate buffers for vertex positions and colors.                                                        |
| [`04_01_sub-allocation_triangle`](examples/04_01_sub-allocation_triangle/) | Storing multiple data regions in one GPU buffer and binding them using offsets.                          |
| [`05_00_uniform`](examples/05_00_uniform/)                                 | Uniform buffers, bind groups, and updating shader parameters from the CPU.                               |
| [`05_01_bindLayoutEntry`](examples/05_01_bindLayoutEntry/)                 | Explicit bind group layouts and pipeline layouts.                                                        |
| [`06_00_depth_culling_msaa`](examples/06_00_depth_culling_msaa/)           | Depth testing, face culling, multisample anti-aliasing, and dynamic uniform offsets.                     |
| [`07_00_texture_samplers`](examples/07_00_texture_samplers/)               | Image uploads, textures, texture views, samplers, and texture coordinates.                               |
| [`08_00_mipmaps`](examples/08_00_mipmaps/)                                 | Generating mip levels and configuring texture filtering.                                                 |
| [`09_00_instanced_rendering`](examples/09_00_instanced_rendering/)         | Drawing multiple objects using instance data.                                                            |
| [`10_00_buffer_mapping`](examples/10_00_buffer_mapping/)                   | Initial buffer mapping, buffer copies, asynchronous mapping, and CPU readback.                           |
| [`11_00_compute`](examples/11_00_compute/)                                 | Compute pipelines, storage buffers, workgroup dispatch, and reading results on the CPU.                  |
| [`11_01_fractal_mandelbort`](examples/11_01_fractal_mandelbort/)           | Computing the Mandelbrot set, pipeline override constants, and displaying the result as ASCII.           |
| [`12_00_rendering_compute`](examples/12_00_rendering_compute/)             | Updating particles with compute and using the same GPU buffer for rendering.                             |
| [`13_00_offscreen_rendering`](examples/13_00_offscreen_rendering/)         | Rendering into a texture and displaying it through a fullscreen post-processing pass.                    |
| [`14_00_workgroup_barriers`](examples/14_00_workgroup_barriers/)           | Workgroup memory, local invocation IDs, and synchronization with `workgroupBarrier()`.                   |
| [`15_00_alpha_blending`](examples/15_00_alpha_blending/)                   | Source and destination colors, alpha blending, and additive blending settings.                           |
| [`16_00_compute_storage_texture`](examples/16_00_compute_storage_texture/) | Writing the Mandelbrot image into a storage texture with compute, then sampling it in a render pipeline. |

The buffer mapping and compute examples in `10_00`, `11_00`, `11_01`, and `14_00` display their results in the terminal.

## Project Structure

* **`examples/`** — example source files and their CMake targets.
* **`vendors/`** — dependencies and supporting code, including GLFW, GLFW–WebGPU integration, and math utilities.
* **`utility/`** — runtime resources used by the examples.
* **`images/`** — images used in the documentation.

Each example has its own `CMakeLists.txt`, but configuration starts from the repository root, where shared dependencies are set up.

## Build Requirements

* **Git** to clone the repository.
* **CMake 3.20 or newer**.
* A **C++20-compatible compiler**.
* A build tool supported by CMake, such as **Ninja**, **Make**, or **MSBuild**.
* A built and installed copy of **Google Dawn**, including its CMake package files.
* Graphics drivers compatible with the Dawn backend used on your system.

GLFW and GLFW–WebGPU integration are built from the sources under `vendors/`. You do not need a separate GLFW installation for this setup. Building GLFW may require additional platform development packages.

### Preparing Dawn

Follow the official [Dawn CMake quickstart](https://github.com/google/dawn/blob/main/docs/quickstart-cmake.md) to build and install Dawn.

Enable `DAWN_ENABLE_INSTALL` when configuring Dawn so its headers, libraries, and CMake package files can be installed.

This repository locates Dawn through `find_package(Dawn REQUIRED)`. If your Dawn installation is outside CMake's default search paths, provide its installation prefix through `CMAKE_PREFIX_PATH`.

## Building

Clone the repository:

```sh
git clone --recurse-submodules https://github.com/con2222/webgpu-examples.git
cd webgpu-examples
```

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

If CMake cannot find Dawn, configure with its installation prefix:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/path/to/dawn/install"
```

If you are configuring CMake on Linux, use the flags for Wayland or X11:
```sh
cmake -S . -B build -DUSE_WAYLAND=ON # or -DUSE_X11=ON
```

Replace `/path/to/dawn/install` with the directory containing your installed Dawn package, rather than its source directory.

### Building One Example

After configuring the project, select an individual target:

```sh
cmake --build build --config Release --target compute_storage_texture --parallel
```

Target names match the main source file names without `.cpp`, such as `hardcode_triangle`, `alpha_blending`, and `compute_storage_texture`.

## Running

**Run the examples with the repository root as the working directory.** Some examples load resources using relative paths such as `utility/texture.jpg`.

With a single-configuration generator such as Ninja or Make, executables are placed in `build/out/`:

```sh
./build/out/compute_storage_texture
```

On Windows, executable names have the `.exe` extension. With Visual Studio, the selected configuration normally adds another directory:

```powershell
.\build\out\Release\compute_storage_texture.exe
```

If you launch an example from an IDE, set its working directory to the repository root.

## Troubleshooting

* **CMake cannot find Dawn:** check that Dawn was installed with its CMake package files, then set `CMAKE_PREFIX_PATH` to the installation prefix.
* **An image fails to load:** check that the required resource exists and that the working directory is the repository root.
* **No window appears:** check whether the example produces terminal output instead.
* **Compilation fails in WebGPU API calls:** check which Dawn revision is installed. Include that revision when reporting an issue.

When reporting a problem, include the example name, operating system, compiler, Dawn revision, and the relevant error output.

## Useful Resources

* [Google Dawn](https://github.com/google/dawn) — the WebGPU implementation used by this repository.
* [Learn WebGPU for C++](https://eliemichel.github.io/LearnWebGPU/) — a detailed introduction to WebGPU with C++ examples.
