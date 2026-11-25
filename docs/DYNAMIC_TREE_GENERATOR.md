# ExoFarm: Dynamic Botanical Tree Generator

## Overview

The **Dynamic Botanical Tree Engine** (`exofarm::BotanicalTreeGenerator` exposed via `BotanicalTreeBridge`, paired with [`BotanicalTreeMaterials`](file:///E:/exofarm/exofarm/scripts/botanical_tree_materials.gd)) is a deterministic 16.16 fixed-point procedural botanical engine built in C++ for ExoFarm and integrated with Godot 4.x. It integrates the core computational and biological models from **Pirk et al. (SIGGRAPH 2012 / ACM TOG)** (*"Plastic Trees: Interactive Self-Adapting Botanical Tree Models"*), featuring:
- **Two-Scale Botanical Architecture**: Macro structural wood skeleton + micro foliage shoot branchlet primitives (`FoliageShoot`) + multi-variant SpeedTree cards.
- **Cluster Translucency & Shadow Volumes (§4.2, Eq. 4)**: Translucent cluster shadow cones ($\gamma_c = \gamma_{c0}^{r_c / r_{c0}}$) with analytical sky hemisphere and sun ray integration.
- **Two-Stage Hierarchical Cladoptosis (§4.4)**:
  - *Stage 1 (Micro-Pruning)*: Shaded foliage shoots ($i(p_c) < \psi_\text{shoot}$) prune first, shedding their cards before parent wood is altered.
  - *Stage 2 (Complete Skeleton Pruning)*: Distal segments and branches that hold 0 living foliage cards are pruned entirely out of the wood mesh, saving bark triangles.
- **Per-Branch 3D Wander & Phototropism (§4.3)**: Decorrelated 3D wander (tangential and binormal deflection) with configurable **lateral vs. vertical wander bias** ($\beta_\text{lat}$ vs $\beta_\text{vert}$) for expressive crown silhouette modeling.
- **SpeedTree Multi-Variant Foliage Atlas**: 3 distinct high-resolution photo-scanned card variations (`birch.png`, `birch_2.png`, `birch_3.png` assembled into `birch_atlas.png`) batched into a single draw call with cambered geometry, tip droop, and two-scale volumetric spherical normals.
- **Integrated 3D Collision Checks**: Ground & slope clearance, exact 3D line segment distance avoidance (Dan Sunday), and obstacle deflection.

---

## Core Architecture

```
                             [ Seed & Species Preset ]
                                       │
                                       ▼
                     [ Dynamic Growth State: g ∈ [0.0, 1.0] ]
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        ▼                              ▼                              ▼
 [ Apical Surge ]            [ Asynchronous Tiers ]       [ Dynamic Tropism Bending ]
 - Front-loaded leader       - Sequential bud emergence   - w_s d_o + (1-w_s) t
 - Muscular root flare       - Early elongation cessation - Sunward photo + gravity droop
        │                              │                              │
        └──────────────────────────────┼──────────────────────────────┘
                                       │
                                       ▼
                   [ Integrated 3D Collision Detection ]
                   - Ground & slope plane clearance (h > 0.18m)
                   - Exact 3D segment-segment distance test
                   - Repulsive space colonization deflection
                   - Obstacle avoidance (Spheres, AABBs, Cylinders)
                                       │
                                       ▼
                 [ Pirk 2012 Cluster Translucency & Shadow Volumes ]
                 - Translucency: γ_c = γ_0^(r_c / r_0) (γ_0 = 0.55)
                 - Multi-ray canopy shadow cones (Sun, Zenith, Diffuse)
                 - Intersecting upper cluster occlusion
                                       │
                                       ▼
                 [ Biological Resource-to-Length Pruning Ratio ]
                 - Gathered energy: ζ_b = Σ 2π r_c^2 · i(p_c)
                 - Supporting wood length: l_b = Σ len(seg)
                 - Pruning condition: ζ_b / l_b < ψ_min
                 - Sheds 100% of foliage cards & desiccates dead wood stubs
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
         [ 3D Bark ArrayMesh ]               [ SpeedTree Multi-Card Mesh ]
         - RMF twist-free extrusion          - Batched single ArrayMesh (1 draw call)
         - 100% Godot outward winding        - 3-column horizontal texture atlas
         - Seamless cylindrical UVs          - Cambered quads with gravity droop
         - Branch collar flares              - Two-scale volumetric spherical normals
```

---

## Core Botanical Systems

### 1. Pirk 2012 Cluster Translucency & Shadow Volumes (§4.2)
- **Cluster Lobes**: Foliage is organized into botanical lobes with position $\mathbf{p}_c$ and radius $r_c$.
- **Translucency Model**:
  $$\gamma_c = \gamma_{c0}^{r_c / r_{c0}}, \quad \gamma_{c0} \approx 0.55, \quad r_{c0} = 0.65\text{m}$$
- **Multi-Ray Hemispherical Integration**:
  Evaluates incoming light across direct sunlight ($\mathbf{d}_\text{sun}$, weight $0.60$), zenith diffuse sky ($\mathbf{d}_\text{zenith}$, weight $0.25$), and lateral ambient sky ($\mathbf{d}_\text{diffuse}$, weight $0.15$). Rays passing through intersecting upper clusters are attenuated multiplicatively by $\gamma_k$ with penumbra edge softening. External obstacles cast solid shadow ($0.0$).

### 2. Biological Resource-to-Length Pruning (§4.4)
- **Photosynthetic Energy Intake**:
  $$\zeta_b = \sum_{c \in C_b} 2\pi r_c^2 \cdot i(\mathbf{p}_c)$$
- **Maintenance Respiration Cost**:
  $$l_b = \sum_{s \in b} \|\mathbf{p}_\text{end} - \mathbf{p}_\text{start}\|$$
- **Pruning Ratio**:
  $$\rho_b = \frac{\zeta_b}{l_b}$$
- If $\rho_b < \psi_\text{min}$ (where $\psi_\text{min} = \text{threshold} \times 2.8$):
  - The branch consumes more energy than its leaves gather.
  - Cladoptosis sheds all foliage cards ($100\%$ card shedding on dead wood).
  - Branch elongation ceases and truncates into a bare dead stub ($l_b' = 0.40 l_b, r' = 0.82 r$).

### 3. Dynamic Tropism Bending (§4.3)
- Heading combines intrinsic direction $\mathbf{d}_o$ and composite tropism $\mathbf{t}$:
  $$\mathbf{h} = w_s \mathbf{d}_o + (1 - w_s) \mathbf{t}$$
- Basal segments have high structural stiffness ($w_s \approx 0.85$); slender outer branchlets have low stiffness ($w_s \approx 0.35$), flexing gracefully toward the sun and drooping under gravity.

### 4. SpeedTree Multi-Variant Foliage Atlas ("Using Cards Better")
- **3-Variant Texture Atlas (`birch_atlas.png`)**:
  - Slot 0 ($U \in [0.0, 0.333]$): Delicate apical shoot with fine stems and fluttering leaflets (`birch.png`).
  - Slot 1 ($U \in [0.333, 0.666]$): Dense bushy cluster with strong lateral branches (`birch_2.png`).
  - Slot 2 ($U \in [0.666, 1.000]$): Pendulous weeping foliage spray (`birch_3.png`).
- **Lobe Clustering Architecture**:
  - Terminal Lobe: 3D fan cluster containing Apical Card (variant 0), Left Wing Card (variant 1, $+36^\circ$ yaw, $-14^\circ$ droop), and Right Wing Card (variant 2, $-36^\circ$ yaw, $-14^\circ$ droop).
  - Mid-Branch Lobe: Alternating weeping flank card along bough flanks.
  - Summit Rosette: 3-card crowning rosette radiating outward from tree apex.
- **Cambered Geometry & Ground Clamping (§5.2)**:
  - Center billows upward ($+0.08 W$), tips curve downward under gravity ($-0.12 L$).
  - Ground collision protection clamps vertices above terrain ($y \ge y_\text{ground} + 0.06\text{m}$).
- **Two-Scale Volumetric Spherical Normals**:
  $$\mathbf{n} = \text{normalize}\left(0.15 \mathbf{n}_\text{quad} + 0.20 \frac{\mathbf{v} - \mathbf{p}_\text{cluster}}{\|\mathbf{v} - \mathbf{p}_\text{cluster}\|} + 0.65 \frac{\mathbf{v} - \mathbf{p}_\text{crown}}{\|\mathbf{v} - \mathbf{p}_\text{crown}\|}\right)$$
  Provides rich individual lobe self-shading while preserving global canopy illumination.

---

## Species Presets

| Preset | Architectural Habit | Crown Silhouette | Typical Height | Branch Angle | Foliage Cards | Pruning Threshold |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`CONIFER`** | Monopodial Excurrent (Pine/Spruce) | Conical | 13.5 m | 68° | 3-Variant Needle Tuft Atlas | 0.22 |
| **`BROADLEAF_OAK`** | Deliquescent (Broadleaf Oak) | Spreading / Domed | 11.0 m | 48° | 3-Way Leaf Fans Atlas | 0.20 |
| **`SLENDER_BIRCH`** | Fastigiate / Pendulous | Slender Oval | 12.5 m | 38° | Photo-Scanned 3-Card Atlas (`birch_atlas.png`) | 0.24 |
| **`DEAD_SNAG`** | Decayed / Weathered | Jagged Stubs | 8.5 m | 65° | Weathered Dry Cards | 0.45 (Heavy) |

---

## Headless Test Results

Run the unified C++ GDExtension & GLSL verification suite:
```powershell
& "C:\Program Files (x86)\Steam\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe" --headless --script res://tests/gd/test_botanical_bridge.gd
```

**Verification Results:**
- **Non-Uniform Growth & Apical Surge**: Passed (Monotonic height/radius, juvenile surge $>65\%$ mature height at $g=0.3$).
- **Directional Phototropism**: Passed (Avg Sunward reach: 1.69 m vs Leeward: 0.61 m).
- **Collision Checks**: Passed (Ground clearance: 100% clear; Min branch distance: 0.454 m; Obstacles avoided: 7).
- **SpeedTree Foliage Cards**: Passed (100% spherical normal outward radiation, Slots 0, 1, 2 distributed across lobes).
- **Two-Stage Cladoptosis & Complete Skeleton Pruning**: Passed (Young: 0 pruned; Mature: 14 pruned, 100% cards shed on dead wood, bare distal skeleton portions culled to save tris).
- **Hierarchical Foliage Shoots**: Passed (Fine foliage shoots prune in shaded interior first before parent wood).
- **Silhouette Wander Bias**: Passed (High lateral bias spread/height 0.61 vs High vertical bias 0.38).
- **Winding Order**: 100.0% Godot outward-facing triangles (`(v2-v0)×(v1-v0)·n0 > 0`).
- **Determinism**: 100% bit-identical hash across resimulations (`0x6B084E85`).
- **DynamicTree Node & Birch Card Integration**: Passed (Loaded `res://assets/cards/birch_atlas.png` $3072 \times 2048$, 242 cards, 2904 foliage tris).
