Mitsuba IM — Physically Based Renderer (Interactive Fork)
=========================================================

[![Build status](https://ci.appveyor.com/api/projects/status/5x9me6a6wtihtojl/branch/master?svg=true&passingText=linux:passing&failingText=linux:failing&pendingText=linux:pending)](https://ci.appveyor.com/project/tszirr/mitsuba-im/branch/master)
[![Build status](https://ci.appveyor.com/api/projects/status/0gvs85hfv3rmv2sm/branch/master?svg=true&passingText=windows:passing&failingText=windows:failing&pendingText=windows:pending)](https://ci.appveyor.com/project/tszirr/mitsuba-im-win/branch/master)
![Immediate-mode UI frontend for mitsuba with interactive preview](http://alphanew.net/refresh/portfolio/immitsuba.jpg)

Mitsuba IM is a fork of the comprehensive physically-based renderer mitsuba (http://mitsuba-renderer.org/) by Wenzel Jakob (and other contributors), which has proven an invaluable framework for the scientific evaluation of both classic rendering algorithms and novel rendering research. This IM fork pursues the following additional goals:

* Responsive interactive preview of rendering algorithms (with interactive camera & settings)
* Easier implementation of new rendering algorithms
	* New simplified interface for responsive integrators (see [include/mitsuba/render/integrator2.h](include/mitsuba/render/integrator2.h))
	* Integrator parameters no longer need to be specified redundantly, they are automatically extracted from the integrator plugins (no modifications required)
	* Comes with responsive wrappers for almost all rendering algorithms in classic mitsuba, which serve as examples
* Responsive imgui frontend that is easily hackable for additional feautres and visualizations (+no more Qt dependencies)
* Compile out of the box on modern C++ compilers (with one recursive git clone)
	* Replace binary dependencies by git submodules
	* Replace boost libraries by C++ standard library
* Compatibility with previous mitsuba interfaces and thus rendering research (a render button still exists, if integrator is not automatically wrappable, interactive preview then falls back to path tracer)

Note: This is a preview release
===============================

## Building

Requires git, CMake, and a compiler with C++17 support (sorry, but at least frees you from boost binaries).

Tested on Ubuntu w/ GCC 7 and on Windows w/ MSVC 2017. You might need to install a GCC 7 package manually.

````
$ git clone https://github.com/tszirr/mitsuba-im --recursive
$ mkdir mitsuba-im/projects
$ cd mitsuba-im/projects
$ cmake ..
(On Windows replace by: $ cmake .. -Ax64)
$ make
$ cd ..
$ ln -s projects/binaries/im-mts
````

On Windows using MSVC 2019 or other variants. Use CMake GUI to configure and generate code into /projects folder. Then open solution in MSVC to build it.

*Be careful: To build in linux, change line 32 in /src/im-mts/CMakeLists.txt as comment says*

## About (Original official description)

http://mitsuba-renderer.org/

Mitsuba is a research-oriented rendering system in the style of PBRT, from which it derives much inspiration. It is written in portable C++, implements unbiased as well as biased techniques, and contains heavy optimizations targeted towards current CPU architectures. Mitsuba is extremely modular: it consists of a small set of core libraries and over 100 different plugins that implement functionality ranging from materials and light sources to complete rendering algorithms.

In comparison to other open source renderers, Mitsuba places a strong emphasis on experimental rendering techniques, such as path-based formulations of Metropolis Light Transport and volumetric modeling approaches. Thus, it may be of genuine interest to those who would like to experiment with such techniques that haven't yet found their way into mainstream renderers, and it also provides a solid foundation for research in this domain.

The renderer currently runs on Linux, MacOS X and Microsoft Windows and makes use of SSE2 optimizations on x86 and x86_64 platforms. So far, its main use has been as a testbed for algorithm development in computer graphics, but there are many other interesting applications.

Mitsuba comes with a command-line interface as well as a graphical frontend to interactively explore scenes. While navigating, a rough preview is shown that becomes increasingly accurate as soon as all movements are stopped. Once a viewpoint has been chosen, a wide range of rendering techniques can be used to generate images, and their parameters can be tuned from within the program.

## Documentation

For compilation, usage, and a full plugin reference, please see the [official documentation](http://mitsuba-renderer.org/docs.html).

## Releases and scenes

Pre-built binaries, as well as example scenes, are available on the [Mitsuba website](http://mitsuba-renderer.org/download.html).
