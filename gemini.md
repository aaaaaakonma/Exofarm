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

Comprehensive documentation is available in the [`docs/`](file:///E:/exofarm/exofarm/docs/README.md) directory:

- [`docs/README.md`](file:///E:/exofarm/exofarm/docs/README.md): Master Engine Architecture Manual and Codebase Sitemap.
- [`docs/DETERMINISTIC_MATH.md`](file:///E:/exofarm/exofarm/docs/DETERMINISTIC_MATH.md): 16.16 fixed-point math, Bhaskara I fast trigonometry, noise suite, and bicubic interpolation.
- [`docs/TECTONIC_WORLDGEN.md`](file:///E:/exofarm/exofarm/docs/TECTONIC_WORLDGEN.md): Continuous Voronoi geodynamics, 7 tectonic boundary regimes, Whittaker biomes, and minerals.
- [`docs/HYDROLOGY_AND_AGRONOMY.md`](file:///E:/exofarm/exofarm/docs/HYDROLOGY_AND_AGRONOMY.md): Closed-loop hydraulic graph, 6-nutrient soil chemistry, crop genetics/affixes, and weather hazards.
- [`docs/AUTOMATION_AND_ECS.md`](file:///E:/exofarm/exofarm/docs/AUTOMATION_AND_ECS.md): Data-oriented SoA ECS, mobile robotics, 16-bit IoT microcontroller VM, and snapshot/rollback.
- [`docs/GDEXTENSION_AND_RENDERING.md`](file:///E:/exofarm/exofarm/docs/GDEXTENSION_AND_RENDERING.md): Godot 4.x GDExtension bridge (`SimBridge`), Jeschke 2020 boundary-aware wave cages, and visualizers.
- [`docs/VERIFICATION_AND_TESTING.md`](file:///E:/exofarm/exofarm/docs/VERIFICATION_AND_TESTING.md): Headless determinism verification tools, test suites, and SCons build instructions.
