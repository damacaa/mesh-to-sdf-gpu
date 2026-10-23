import io
import os
import zipfile
import numpy as np
from PIL import Image

def export_stbn(zip_path: str, out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as z:
        # Pack stbn0: RGB = stbn_vec3, A = stbn_scalar
        buf0 = bytearray(128 * 128 * 64 * 4)
        for i in range(64):
            v3_data = z.read(f"STBN/stbn_vec3_2Dx1D_128x128x64_{i}.png")
            sc_data = z.read(f"STBN/stbn_scalar_2Dx1Dx1D_128x128x64x1_{i}.png")
            v3_arr = np.array(Image.open(io.BytesIO(v3_data)))
            sc_arr = np.array(Image.open(io.BytesIO(sc_data)))

            combined = np.empty((128, 128, 4), dtype=np.uint8)
            combined[:, :, :3] = v3_arr[:, :, :3]
            combined[:, :, 3] = sc_arr
            offset = i * 128 * 128 * 4
            buf0[offset:offset + 128 * 128 * 4] = combined.tobytes()

        with open(os.path.join(out_dir, "stbn0.bin"), "wb") as f:
            f.write(buf0)
        print("Wrote stbn0.bin:", len(buf0), "bytes")

        # Pack stbn1: RG = stbn_vec2, B = stbn_vec1, A = stbn_scalar (slice (i+32)%64)
        buf1 = bytearray(128 * 128 * 64 * 4)
        for i in range(64):
            v2_data = z.read(f"STBN/stbn_vec2_2Dx1D_128x128x64_{i}.png")
            v1_data = z.read(f"STBN/stbn_vec1_2Dx1D_128x128x64_{i}.png")
            sc_data = z.read(f"STBN/stbn_scalar_2Dx1Dx1D_128x128x64x1_{(i + 32) % 64}.png")

            v2_arr = np.array(Image.open(io.BytesIO(v2_data)))
            v1_arr = np.array(Image.open(io.BytesIO(v1_data)))
            sc_arr = np.array(Image.open(io.BytesIO(sc_data)))

            combined = np.empty((128, 128, 4), dtype=np.uint8)
            combined[:, :, :2] = v2_arr[:, :, :2]
            combined[:, :, 2] = v1_arr[:, :, 0]
            combined[:, :, 3] = sc_arr
            offset = i * 128 * 128 * 4
            buf1[offset:offset + 128 * 128 * 4] = combined.tobytes()

        with open(os.path.join(out_dir, "stbn1.bin"), "wb") as f:
            f.write(buf1)
        print("Wrote stbn1.bin:", len(buf1), "bytes")

if __name__ == "__main__":
    zip_candidate = os.path.join("build", "stbn_repo", "Assets", "STBN.zip")
    if os.path.exists(zip_candidate):
        export_stbn(zip_candidate, "assets/stbn")
    else:
        print("STBN.zip not found at", zip_candidate)
