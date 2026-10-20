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
import tempfile
from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

# Path to compiled color_cli executable (relative to this file)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CLI_EXE  = os.path.join(BASE_DIR, "..", "build",
                        "color_cli.exe" if os.name == "nt" else "color_cli")


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

    # The C++ engine does the work: K-Means++ in CIE Lab, centroids named by
    # ΔE2000. The upload goes through a temp file because the CLI reads a path.
    fd, path = tempfile.mkstemp(suffix=".img")
    try:
        with os.fdopen(fd, "wb") as f:
            request.files["image"].save(f)
        result = subprocess.run(
            [CLI_EXE, "--palette", path, "6"],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            return jsonify({"error": result.stderr.strip() or "Could not read image"}), 400
        return jsonify(json.loads(result.stdout.strip()))

    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        os.remove(path)


if __name__ == "__main__":
    if not os.path.exists(CLI_EXE):
        print(f"ERROR: color_cli.exe not found at {CLI_EXE}")
        print("Build the project first: cd build && cmake --build .")
        exit(1)
    print("ColorVision++ Web  →  http://localhost:5000")
    app.run(debug=True, port=5000)
