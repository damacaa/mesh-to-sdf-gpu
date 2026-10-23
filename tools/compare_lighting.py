"""Show lighting quality and timing from a completed benchmark run."""

import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--stage", choices=["raw", "temporal", "filtered"], default="filtered")
    parser.add_argument("--baseline", help="Show percentage changes against this case.")
    args = parser.parse_args()
    manifest = json.loads((args.directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest["status"] != "complete":
        parser.error("The benchmark is incomplete.")
    results = json.loads((args.directory / "results.json").read_text(encoding="utf-8"))
    rows = [row for row in results["aggregates"] if row["stage"] == args.stage]
    metrics = ["rmse", "flicker_rms", "temporal_std", "mean_image_rmse", "gpu_ms_mean"]
    baseline = next((row for row in rows if row["case"] == args.baseline), None)
    if args.baseline and baseline is None:
        parser.error("The baseline case does not exist.")
    print(f"Scene: {manifest['scene_name']}; stage: {args.stage}; GPU: {manifest['gpu']}")
    print(f"Estimated reference noise: {manifest['reference_estimated_rmse']:.6g} RGB RMSE")
    print("Values show the repeat mean and sample standard deviation. Lower values indicate less error or variation.")
    print("| Case | RGB RMSE | Flicker RMS | Temporal SD | Mean-image RMSE | GPU ms |")
    print("|---|---:|---:|---:|---:|---:|")
    for row in rows:
        fields = []
        for metric in metrics:
            value = row["metrics"][metric]
            text = f"{value['mean']:.6g}"
            if value["repeat_stddev"] is not None:
                text += f" +/- {value['repeat_stddev']:.2g}"
            if baseline and row is not baseline:
                denominator = baseline["metrics"][metric]["mean"]
                if denominator:
                    text += f" ({100 * (value['mean'] / denominator - 1):+.1f}%)"
            fields.append(text)
        print("| " + " | ".join([row["case"], *fields]) + " |")
    print("Flicker alone cannot distinguish a stable image from excessive blur. Compare it with image error and saved images.")
    print("GPU times describe the serialized benchmark. They do not measure interactive frame rates.")


if __name__ == "__main__":
    main()
