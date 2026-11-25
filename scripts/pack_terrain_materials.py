import os
from PIL import Image

MATERIAL_DEFS = [
    {"id": "Grass005", "dir": "Grass005_2K-JPG", "desc": "Clean Short Lawn Turf"},
    {"id": "Grass006", "dir": "Grass006_2K-JPG", "desc": "Lush Meadow & Clover"},
    {"id": "Ground033", "dir": "Ground033_2K-JPG", "desc": "Desert Sand Dunes"},
    {"id": "Ground100", "dir": "Ground100_2K-JPG", "desc": "Intertidal Sand Ripples"},
    {"id": "Ground083", "dir": "Ground083_2K-JPG", "desc": "Wet Clay Bank & Mud"},
    {"id": "Ground104", "dir": "Ground104_2K-JPG", "desc": "Packed Agricultural Dirt"},
    {"id": "Gravel014", "dir": "Gravel014_2K-JPG", "desc": "Crushed Scree in Sand"},
    {"id": "Gravel020", "dir": "Gravel020_2K-JPG", "desc": "Rounded River Cobbles"},
    {"id": "Gravel022", "dir": "Gravel022_2K-JPG", "desc": "Multi-tone Stream Pebbles"},
    {"id": "Gravel036L", "dir": "Gravel036L_2K-JPG", "desc": "Fine Granite Grit"},
    {"id": "Rock028", "dir": "Rock028_2K-JPG", "desc": "Weathered Slate/Schist (Default Cliff)"},
    {"id": "Rock030", "dir": "Rock030_2K-JPG", "desc": "Sedimentary Rock (Canyons)"},
    {"id": "Rock031", "dir": "Rock031_2K-JPG", "desc": "Dark Charcoal Shale (Ravines)"},
    {"id": "Rock063", "dir": "Rock063_2K-JPG", "desc": "Mossy Boulders (Jungle/Forest)"},
]

BASE_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "assets", "material"))
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "assets", "material_packed"))
TARGET_RES = 1024

def pack_materials():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Packing {len(MATERIAL_DEFS)} materials at {TARGET_RES}x{TARGET_RES}...")

    for idx, mat in enumerate(MATERIAL_DEFS):
        mat_id = mat["id"]
        dir_name = mat["dir"]
        prefix = f"{mat_id}_2K-JPG"
        mat_path = os.path.join(BASE_DIR, dir_name)

        print(f"[{idx:02d}/{len(MATERIAL_DEFS)-1:02d}] Processing {mat_id} ({mat['desc']})...")

        # 1. Albedo / Color
        color_file = os.path.join(mat_path, f"{prefix}_Color.jpg")
        color_img = Image.open(color_file).convert("RGB")
        if color_img.size != (TARGET_RES, TARGET_RES):
            color_img = color_img.resize((TARGET_RES, TARGET_RES), Image.Resampling.LANCZOS)
        out_albedo = os.path.join(OUT_DIR, f"slice_{idx:02d}_{mat_id}_albedo.jpg")
        color_img.save(out_albedo, quality=90)

        # 2. NormalGL (Saved as lossless PNG to avoid compression artifacts in normal vectors)
        norm_file = os.path.join(mat_path, f"{prefix}_NormalGL.jpg")
        if not os.path.exists(norm_file):
            norm_file = os.path.join(mat_path, f"{prefix}_NormalGL.png")
        norm_img = Image.open(norm_file).convert("RGB")
        if norm_img.size != (TARGET_RES, TARGET_RES):
            norm_img = norm_img.resize((TARGET_RES, TARGET_RES), Image.Resampling.LANCZOS)
        out_normal = os.path.join(OUT_DIR, f"slice_{idx:02d}_{mat_id}_normal.png")
        norm_img.save(out_normal, format="PNG")

        # 3. ORM (R = AO, G = Roughness, B = Displacement/Height, saved as lossless PNG)
        ao_file = os.path.join(mat_path, f"{prefix}_AmbientOcclusion.jpg")
        ao_img = Image.open(ao_file).convert("L") if os.path.exists(ao_file) else Image.new("L", (TARGET_RES, TARGET_RES), 255)
        if ao_img.size != (TARGET_RES, TARGET_RES):
            ao_img = ao_img.resize((TARGET_RES, TARGET_RES), Image.Resampling.LANCZOS)

        rough_file = os.path.join(mat_path, f"{prefix}_Roughness.jpg")
        rough_img = Image.open(rough_file).convert("L") if os.path.exists(rough_file) else Image.new("L", (TARGET_RES, TARGET_RES), 200)
        if rough_img.size != (TARGET_RES, TARGET_RES):
            rough_img = rough_img.resize((TARGET_RES, TARGET_RES), Image.Resampling.LANCZOS)

        disp_file = os.path.join(mat_path, f"{prefix}_Displacement.jpg")
        disp_img = Image.open(disp_file).convert("L") if os.path.exists(disp_file) else Image.new("L", (TARGET_RES, TARGET_RES), 128)
        if disp_img.size != (TARGET_RES, TARGET_RES):
            disp_img = disp_img.resize((TARGET_RES, TARGET_RES), Image.Resampling.LANCZOS)

        orm_img = Image.merge("RGB", (ao_img, rough_img, disp_img))
        out_orm = os.path.join(OUT_DIR, f"slice_{idx:02d}_{mat_id}_orm.png")
        orm_img.save(out_orm, format="PNG")

    print("All materials successfully packed into", OUT_DIR)

if __name__ == "__main__":
    pack_materials()
