# ExoFarm Terrain Materials Catalog & Integration Guide

This document provides a visual analysis, channel specification, and engine integration guide for the 14 PBR materials downloaded from ambientCG into [`assets/material/`](file:///E:/exofarm/exofarm/assets/material).

Every material was inspected in 2K resolution across its individual PBR maps (`Color`, `NormalGL`, `Roughness`, `Displacement`, and `AmbientOcclusion`).

---

## 1. Quick Reference & System Matrix

| Material ID | Category | Primary Visual Trait | Best ExoFarm System / Biome | Recommended Shader Role |
| :--- | :--- | :--- | :--- | :--- |
| **`Grass005`** | Vegetation | Clean, uniform short grass lawn | Grassland / Savanna (`biome 3`) | Under-turf base for 3D grass cards |
| **`Grass006`** | Vegetation | Lush meadow with clovers & broadleaf | Forest clearings, orchard ground (`biome 4`) | Secondary grass / lush field blend |
| **`Gravel014`**| Granular | Medium crushed rock in beige sandy soil | Arid transitions, desert edges, dry wash | Height-blended scree transition |
| **`Gravel020`**| Hydrology | Smooth, rounded river cobbles | River reaches, lake beds, brook conduits | Underwater bed & splash zone |
| **`Gravel022`**| Hydrology | Small multi-tone rounded pebbles | Headwater springs, shallow stream runs | Fluvial gravel & path trim |
| **`Gravel036L`**| Granular | Fine uniform crushed gray granite grit | Mountain talus, quarry paths, robot roads | High-compactness soil / road overlay |
| **`Ground033`**| Arid | Wind-rippled warm sand dunes | Hot Desert (`biome 2`), upper dry beach | Macro desert / dune surface |
| **`Ground083`**| Agronomy | Damp squishy clay mud with olive algae | Swamp (`biome 6`), estuary banks, flood zones | Wet soil / tilled plot base |
| **`Ground100`**| Hydrology | Wave-sculpted sand ripple crests | Seabed shelf ($H \le 0$), intertidal swash | Underwater bathymetry & tidal flat |
| **`Ground104`**| Agronomy | Compacted agricultural soil with ruts | Farm fields, tilled plots, robot tracks | Tilled soil / agronomic plots |
| **`Rock028`**  | Orogeny | Weathered slate/schist with moss cracks | Collision mountain belts, alpine crags | Default triplanar cliff ($>35^\circ$) |
| **`Rock030`**  | Orogeny | Layered sedimentary rock with shelves | Canyons, transverse water gaps, rift shoulders| Horizontal strata canyon walls |
| **`Rock031`**  | Orogeny | Dark charcoal shale with steep overhangs | Deep river gorges, fault scarps, shaded cliffs | Shadowed ravines & dark scarps |
| **`Rock063`**  | Orogeny | Boulders draped in dense green moss | Rainforest (`biome 5`), Forest cliffs (`biome 4`)| Shaded & high-moisture rock faces |

---

## 2. Detailed Material Profiles

### Vegetation & Turf

#### `Grass005` (Clean Grass Green Lawn Short)
- **Path:** [`assets/material/Grass005_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Grass005_2K-JPG/)
- **Visual Analysis:** Fine, uniform, short-cut grass lawn with minimal contrast variations and no prominent weeds or dandelion heads. 
- **PBR Characteristics:** Smooth displacement variation; roughness is consistently matte ($0.75\text{–}0.85$); normal map provides subtle micro-blade relief without high-frequency noise.
- **Engine Application:**
  - **3D Foliage Under-Turf:** Serves as the base carpet underneath procedural 3D grass cards/instances. Eliminates the perspective clash of painted flat grass blades while matching the root vertex color of 3D foliage.
  - **Grassland Biome:** Primary flat ground albedo for Whittaker Biome 3.

#### `Grass006` (Lush Clover & Wild Meadow Turf)
- **Path:** [`assets/material/Grass006_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Grass006_2K-JPG/)
- **Visual Analysis:** Rich, deep green ground cover speckled with small clovers, wild broadleaf weeds, and micro organic debris.
- **PBR Characteristics:** Crisp normal map detailing clover leaf cups; displacement map has distinctive small leaf clusters.
- **Engine Application:**
  - **Temperate Forest Clearings:** Ground cover beneath canopy trees where sunlight filters through.
  - **Moisture Splat Blend:** Procedurally blend into `Grass005` in high-moisture valleys ($M > 12000$) to represent lush vegetative growth.

---

### Hydrology & Water Boundaries

#### `Ground100` (Wave-Sculpted Intertidal Sand)
- **Path:** [`assets/material/Ground100_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Ground100_2K-JPG/)
- **Visual Analysis:** Prominent parallel wave ripple crests sculpted by oscillating water currents.
- **PBR Characteristics:** Dramatic normal and displacement maps with clean parabolic wave ridges; wet roughness response ($0.30\text{–}0.50$ when submerged).
- **Engine Application:**
  - **Submerged Continental Shelf:** Applied to elevations $H \le 0.0\,\text{m}$ under the `wave_water.gdshader`.
  - **Tidal Swash Zone:** Shallows and estuary mouths where wave particles shoal.

#### `Gravel020` (Rounded River Cobbles)
- **Path:** [`assets/material/Gravel020_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Gravel020_2K-JPG/)
- **Visual Analysis:** Smooth, rounded, water-polished gray river stones tightly packed into a riverbed matrix.
- **PBR Characteristics:** Deep AO in pebble interspaces; high displacement variation between stone crowns and recesses.
- **Engine Application:**
  - **River Corridors:** Directly burns into tiles mapped by `RiverBelt` (tiers 1, 2, and 3).
  - **Lake Edges & Headwater Basins:** Natural shore gravel where sediment settles.

#### `Gravel022` (Multi-Tone Stream Pebbles)
- **Path:** [`assets/material/Gravel022_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Gravel022_2K-JPG/)
- **Visual Analysis:** Fine, colorful mix of slate-gray, olive-green, and sandstone-brown rounded pebbles.
- **PBR Characteristics:** Extremely balanced color variation; tiles tightly without noticeable seams.
- **Engine Application:**
  - **Mountain Rills & Creeks:** Foothill streams and tributary reaches.
  - **Farm Landscaping & Conduits:** Decorative drainage aggregate surrounding water pumps and irrigation pipes.

---

### Arid & Granular Terrain

#### `Ground033` (Wind-Rippled Desert Sand)
- **Path:** [`assets/material/Ground033_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Ground033_2K-JPG/)
- **Visual Analysis:** Clean, warm tan sand dunes sculpted by prevailing winds with soft sinusoidal crests.
- **PBR Characteristics:** Subtle, soft normal displacement; high roughness ($0.88\text{–}0.92$) preventing plastic specular glints.
- **Engine Application:**
  - **Hot & Cold Desert:** Whittaker Biome 2 ($M < -14000$).
  - **Upper Coastal Beach:** Dry dunes above the wave-wash waterline ($H > 3.0\,\text{m}$).

#### `Gravel014` (Crushed Rock in Sandy Soil)
- **Path:** [`assets/material/Gravel014_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Gravel014_2K-JPG/)
- **Visual Analysis:** Coarse jagged crushed rock embedded in a firm sandy-beige soil matrix.
- **PBR Characteristics:** High-contrast displacement between rock chips and flat sand.
- **Engine Application:**
  - **Ecotone Transitions:** Splat boundary between Desert sand (`Ground033`) and mountain foothills.
  - **Dry Wash & Arroyo:** Ephemeral stream channels during dry weather cycles.

#### `Gravel036L` (Fine Crushed Granite Aggregate)
- **Path:** [`assets/material/Gravel036L_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Gravel036L_2K-JPG/)
- **Visual Analysis:** Neutral gray crushed aggregate, uniform fine grain.
- **PBR Characteristics:** Monolithic micro-relief; minimal directional bias, ideal for tiling.
- **Engine Application:**
  - **Compacted Soil Roads:** Industrial tracks carved by player vehicles or automation robots.
  - **Mountain Talus Aprons:** Scree aprons formed at the base of cliffs by `dem_erosion.h` talus relaxation.

---

### Agronomy & Soil Genesis

#### `Ground083` (Muddy Clay Bank with Silt)
- **Path:** [`assets/material/Ground083_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Ground083_2K-JPG/)
- **Visual Analysis:** Squishy, saturated clay mud featuring organic silt, small pebbles, and a faint olive-green algae/moss sheen.
- **PBR Characteristics:** Varied roughness ($0.25$ in wet depressions, $0.65$ on mud mounds); slick specular response.
- **Engine Application:**
  - **Swamp / Wetlands:** Whittaker Biome 6 (`MUD_BANK`).
  - **Over-Saturated Soil:** Visual cue when soil moisture exceeds $85\%$ due to heavy rain or over-irrigation.
  - **Estuary Plume Margins:** Mud banks where rivers discharge sediment into the ocean.

#### `Ground104` (Packed Agricultural Soil & Field Ruts)
- **Path:** [`assets/material/Ground104_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Ground104_2K-JPG/)
- **Visual Analysis:** Crumbly, nutrient-rich dirt mixed with small stones, displaying linear furrow ruts from tilling or vehicle passes.
- **PBR Characteristics:** Tactile crumbly normal map; displacement allows sharp height-blending against grass.
- **Engine Application:**
  - **Tilled Farming Plots:** Player-tilled farm rows and crop planting beds.
  - **Compacted Ground:** Ground with compactness $>50\%$ in the agronomy simulation.

---

### Tectonic Cliffs & Rock Strata

#### `Rock028` (Weathered Metamorphic Slate / Schist)
- **Path:** [`assets/material/Rock028_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Rock028_2K-JPG/)
- **Visual Analysis:** Medium-to-dark gray slate with sharp angular joint fractures and subtle lichen in fissures.
- **PBR Characteristics:** Crisp, deep normal crevices; neutral color temperature.
- **Engine Application:**
  - **Primary Cliff Triplanar Material:** Universal vertical rock face for slopes $>35^\circ$.
  - **Alpine Collision Belts:** Mountain fold belts created by continental collision ($C > C_{\text{thresh}}$).

#### `Rock030` (Layered Sedimentary Rock with Ledges)
- **Path:** [`assets/material/Rock030_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Rock030_2K-JPG/)
- **Visual Analysis:** Distinct horizontal bedding planes with stepped ledges and warm limestone/sandstone tones.
- **PBR Characteristics:** Strong directional bedding normal; high displacement shelves.
- **Engine Application:**
  - **River Canyons & Gorges:** Carved valley walls where water has cut through horizontal rock layers.
  - **Rift Basins & Fault Scarps:** Continental rift shoulders.

#### `Rock031` (Dark Charcoal Shale & Overhangs)
- **Path:** [`assets/material/Rock031_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Rock031_2K-JPG/)
- **Visual Analysis:** Dramatic, dark charcoal-black shale with sharp protruding shelves and yellow-green micro-lichen.
- **PBR Characteristics:** High visual contrast; dark albedo creates deep natural shadows.
- **Engine Application:**
  - **Deep Shadowed Canyons:** North-facing or steep shear scarps ($S > 3000$).
  - **Volcanic Subduction Flanks:** Volcanic arcs transitioning into dark rock.

#### `Rock063` (Mossy Forest Boulders & Cliffs)
- **Path:** [`assets/material/Rock063_2K-JPG/`](file:///E:/exofarm/exofarm/assets/material/Rock063_2K-JPG/)
- **Visual Analysis:** Weathered limestone rock heavily encrusted with lush green moss cushions.
- **PBR Characteristics:** Softened normal map on moss pads; variable roughness ($0.85$ on moss, $0.55$ on exposed stone).
- **Engine Application:**
  - **Forest & Rainforest Cliffs:** Rock faces located in high-moisture biomes (Biomes 4 & 5).
  - **Waterfall & Stream Boulders:** Moist rocks adjacent to active river corridors.

---

## 3. Shader Integration Strategy (`Texture2DArray`)

To avoid exceeding Godot 4's maximum texture sampler limit (typically 16 samplers per shader stage), pack these materials into **Texture Arrays**:

1. **Albedo Array (`Texture2DArray`):** 14 slices ($2048 \times 2048$, sRGB).
2. **NormalGL Array (`Texture2DArray`):** 14 slices ($2048 \times 2048$, Linear).
3. **ORM Array (`Texture2DArray`):** 14 slices combining:
   - **R:** Ambient Occlusion (`_AmbientOcclusion.jpg`)
   - **G:** Roughness (`_Roughness.jpg`)
   - **B:** Height / Displacement (`_Displacement.jpg`)

This reduces texture lookups from **42 separate samplers down to just 3 sampler uniforms** in `world_terrain_3d.gdshader`.
