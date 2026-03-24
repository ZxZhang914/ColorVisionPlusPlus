"""
ColorVision++ Web API
Flask backend that wraps the C++ color_cli executable.

Run:  python app.py
Then: open http://localhost:5000
"""

import subprocess
import json
import os
import re
import io
from flask import Flask, request, jsonify, render_template
from PIL import Image

app = Flask(__name__)

# Path to compiled color_cli executable (relative to this file)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CLI_EXE  = os.path.join(BASE_DIR, "..", "build", "color_cli.exe")


def call_classifier(r: int, g: int, b: int) -> dict:
    """Call color_cli --json R G B and parse the result."""
    result = subprocess.run(
        [CLI_EXE, "--json", str(r), str(g), str(b)],
        capture_output=True, text=True, timeout=5
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip())
    return json.loads(result.stdout.strip())


def hex_to_rgb(hex_str: str):
    hex_str = hex_str.lstrip("#")
    if not re.fullmatch(r"[0-9A-Fa-f]{6}", hex_str):
        raise ValueError(f"Invalid HEX code: {hex_str}")
    r = int(hex_str[0:2], 16)
    g = int(hex_str[2:4], 16)
    b = int(hex_str[4:6], 16)
    return r, g, b


# ── Routes ────────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/classify", methods=["POST"])
def classify():
    data = request.get_json(force=True)

    try:
        if "hex" in data:
            r, g, b = hex_to_rgb(data["hex"])
        else:
            r = int(data["r"])
            g = int(data["g"])
            b = int(data["b"])

        if not all(0 <= v <= 255 for v in (r, g, b)):
            return jsonify({"error": "RGB values must be 0-255"}), 400

        result = call_classifier(r, g, b)
        return jsonify(result)

    except (KeyError, ValueError, TypeError) as e:
        return jsonify({"error": str(e)}), 400
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route("/api/palette", methods=["POST"])
def palette():
    if "image" not in request.files:
        return jsonify({"error": "No image file provided"}), 400

    try:
        img = Image.open(io.BytesIO(request.files["image"].read())).convert("RGB")
        # Resize for speed, then quantize to K colors via median-cut
        img.thumbnail((200, 200))
        quantized = img.quantize(colors=6, method=Image.Quantize.MEDIANCUT)
        raw_palette = quantized.getpalette()[:6 * 3]  # first 6 colors, 3 channels each

        colors = []
        for i in range(6):
            r, g, b = raw_palette[i*3], raw_palette[i*3+1], raw_palette[i*3+2]
            analysis = call_classifier(r, g, b)
            colors.append(analysis)

        return jsonify({"colors": colors})

    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    if not os.path.exists(CLI_EXE):
        print(f"ERROR: color_cli.exe not found at {CLI_EXE}")
        print("Build the project first: cd build && cmake --build .")
        exit(1)
    print("ColorVision++ Web  →  http://localhost:5000")
    app.run(debug=True, port=5000)
