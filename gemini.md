# ExoFarm Engine Codebase Guide

This file provides an overview of the directories and files within the ExoFarm deterministic engine codebase. The engine is built using **GDExtension (C++)** with **16.16 fixed-point math** for lockstep deterministic simulation, tailored for farming simulation, ecology, and automation gameplay.

---

## Folder Structure

```
E:/exofarm/exofarm/
├── src/                      # Core engine source files
│   ├── core/                 # Core engine systems, collision, items, and farming simulation
│   ├── entities/             # Game entities (Player, Enemy, Wall)
│   ├── math/                 # Deterministic math utilities (fixed point, CORDIC, RNG)
│   └── gdextension/          # Godot C++ GDExtension bindings
├── test_determinism.cpp      # Standalone headless verification tool (runs lockstep simulation check)
├── test_biomes.cpp           # Headless biome distribution analyzer
└── SConstruct                # SCons build configuration
```

---

## File Sitemap

