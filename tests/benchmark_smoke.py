"""Check the complete benchmark with small, repeatable GPU workloads."""

import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def run(executable, config, output, succeeds=True):
    result = subprocess.run(
        [executable, "--config", str(config), "--output", str(output)],
        capture_output=True, text=True, timeout=120,
    )
    if (result.returncode == 0) != succeeds:
        raise AssertionError(result.stdout + result.stderr)


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="lighting_benchmark_") as temporary:
        root = Path(temporary)
        config = {
            "width": 24, "height": 16, "scene": 1, "frames": 4,
            "warmup_frames": 2, "reference_frames": 8, "seeds": [17],
            "cases": [
                {"name": "white", "noise": "white", "atrous_iterations": 0},
                {"name": "blue", "noise": "blue", "atrous_iterations": 4},
                {"name": "demodulated", "noise": "white", "demodulate_albedo": True,
                 "atrous_iterations": 1, "max_history_frames": 64},
            ],
        }
        path = root / "config.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        for name in ("first", "second"):
            run(executable, path, root / name)
        first = json.loads((root / "first/results.json").read_text())
        second = json.loads((root / "second/results.json").read_text())
        assert len(first["runs"]) == 3
        for left, right in zip(first["runs"], second["runs"]):
            assert left["stages"] == right["stages"], "Quality metrics changed between identical runs."
            assert left["gpu_ms_mean"] > 0
        white = first["runs"][0]["stages"]
        assert white["temporal"] == white["filtered"]
        for image in (root / "first").rglob("*.pfm"):
            assert image.read_bytes() == (root / "second" / image.relative_to(root / "first")).read_bytes()
        manifest = json.loads((root / "first/manifest.json").read_text())
        assert manifest["status"] == "complete" and manifest["mask_pixels"] > 0
        with (root / "first/frames.csv").open(newline="") as handle:
            rows = list(csv.DictReader(handle))
        assert len(rows) == 3 * 4 * 3
        assert sum(row["flicker_rms"] == "" for row in rows) == 3 * 3
        original = (root / "first/results.json").read_bytes()
        run(executable, path, root / "first", succeeds=False)
        assert original == (root / "first/results.json").read_bytes()
        config["cases"][0]["temporal_aplha"] = 0.5
        path.write_text(json.dumps(config), encoding="utf-8")
        run(executable, path, root / "invalid", succeeds=False)
        assert not (root / "invalid").exists()
        del config["cases"][0]["temporal_aplha"]
        config["cases"][1]["name"] = "WHITE"
        path.write_text(json.dumps(config), encoding="utf-8")
        run(executable, path, root / "collision", succeeds=False)
        assert not (root / "collision").exists()
    print("PASS: deterministic metrics and PFM images, stage selection, output protection, and strict configuration")


if __name__ == "__main__":
    main()
