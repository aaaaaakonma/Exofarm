#!/usr/bin/env python
import os
import sys

# Load the SConscript from godot-cpp
# This will return the environment configured by godot-cpp
env = SConscript("godot-cpp/SConstruct")

# MSVC on Windows, GCC/Clang on others, with optimized flags
if env["platform"] == "windows":
    # MSVC: High optimization, with LTO
    env.Append(CCFLAGS=[
        "/O2",       # Maximize Speed
        "/Ob2",      # Inline Any Suitable
        "/Oi",       # Enable Intrinsic Functions
        "/Ot",       # Favor Fast Code
        "/Oy",       # Omit Frame Pointers
        "/fp:fast",  # Fast floating point behavior (MSVC's -ffast-math)
        "/Qfast_transcendentals", # Generates inline code for transcendental functions
        "/GL",       # Whole Program Optimization
        "/GF",       # Eliminate Duplicate Strings
        "/Gy",       # Enable Function-Level Linking
        "/Gw",       # Optimize Global Data
        "/arch:AVX2", # Use AVX2 instructions
        "/DNDEBUG"   # Disable assertions
    ])
    env.Append(CXXFLAGS=["/vmg", "/std:c++17", "/EHsc"])
    env.Append(LINKFLAGS=["/DEBUG", "/LTCG"])
else:
    # GCC/Clang for Linux/macOS
    env.Append(CCFLAGS=[
        "-O3", 
        "-ffast-math",
        "-march=native",
        "-mtune=native",
        "-flto=thin",
        "-fno-math-errno",
        "-fno-trapping-math",
        "-fno-signed-zeros",
        "-fno-associative-math",
        "-funroll-loops",
        "-fvectorize",
        "-fslp-vectorize",
        "-fprefetch-loop-arrays",
        "-fomit-frame-pointer",
        "-DNDEBUG",
        "-fvisibility=hidden"
    ])
    env.Append(LINKFLAGS=["-flto=thin"])


# Add the src directory and all its subdirectories to the include path
src_dir = "src"
cpp_paths = [src_dir]
for root, dirs, files in os.walk(src_dir):
    for d in dirs:
        cpp_paths.append(os.path.join(root, d).replace('\\', '/'))

env.Append(CPPPATH=cpp_paths)

# Source files to compile
# We include all .cpp files recursively in the src directory
sources = []
for root, dirs, files in os.walk(src_dir):
    for f in files:
        if f.endswith(".cpp"):
            sources.append(os.path.join(root, f).replace('\\', '/'))


# Target library name
lib_name = "mecha_arpg"

# Determine the library path and name based on the platform
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "bin/lib{}.{}.{}.framework/lib{}.{}.{}".format(
            lib_name, env["platform"], env["target"], lib_name, env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "bin/lib{}.{}.{}.simulator.a".format(lib_name, env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "bin/lib{}.{}.{}.a".format(lib_name, env["platform"], env["target"]),
            source=sources,
        )
else:
    # Windows and Linux
    library = env.SharedLibrary(
        "bin/lib{}{}{}".format(lib_name, env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

# Disable caching for the library itself
env.NoCache(library)

# Set the library as the default target
Default(library)

# --- Headless Targets ---
# Standalone tools for performance testing and deterministic verification
# These use the SIM_HEADLESS define to strip Godot dependencies.
headless_env = env.Clone()
headless_env.Append(CPPDEFINES=["SIM_HEADLESS"])

headless_env.Prepend(CPPPATH=["."]) 

determinism_test = headless_env.Program(
    target="bin/test_determinism",
    source="test_determinism.cpp"
)
Alias("test", determinism_test)

biomes_test = headless_env.Program(
    target="bin/test_biomes",
    source="test_biomes.cpp"
)
Alias("biomes", biomes_test)

hydrology_test = headless_env.Program(
    target="bin/test_hydrology",
    source="test_hydrology.cpp"
)
Alias("hydrology", hydrology_test)
