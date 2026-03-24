# ColorVision++ — Perceptual Color Analysis Engine

A real-time color detection and classification system in C++17 + OpenCV that implements
**CIE L\*a\*b\* colorimetry**, the **CIEDE2000 perceptual distance metric** (Sharma et al. 2005),
**K-Means++ dominant color extraction** in Lab space, and **clinical colorblindness simulation**
via Daltonization matrices — with a quantitative benchmark comparing two classification approaches.

---

## Benchmark Results

| Metric | HSV baseline | ΔE2000 nearest-neighbor |
|---|---|---|
| Broad-category accuracy (11 classes) | 64% | **84%** (+20 pp) |
| Mean ΔE2000 perceptual error | 9.33 | **5.63** (40% lower) |
| Throughput | 11M cls/sec | 26K cls/sec |

Dataset: 129 named colors sampled from the XKCD Color Survey (Monroe 2010), 80/20 train/test split.

---

## Features

### 1. CIE L\*a\*b\* Colorimetry Pipeline
- sRGB → CIE XYZ (D65 illuminant, IEC 61966-2-1 linearization)
- CIE XYZ → CIE L\*a\*b\* (piecewise cube-root transfer function, D65 white point)
- Full implementation from primary standards — not a library wrapper

### 2. CIEDE2000 Perceptual Color Difference
- Complete implementation of Sharma et al. (2005) including:
  - G-factor chroma adjustment for the blue-purple region
  - Hue-angle quadrant correction
  - Parametric weighting functions S_L, S_C, S_H
  - R_T rotation term (blue-purple anomaly correction)
- Used for nearest-neighbor color naming and dominant color labeling

### 3. K-Means++ Dominant Color Extraction
- Custom K-Means++ implementation (not `cv::kmeans`) with D² seeding
- Clustering in Lab space: perceptually uniform, substantially better than Euclidean RGB
- ΔE2000 applied post-clustering to name each centroid (K calls per frame, not per-pixel)
- Real-time palette strip displayed at the bottom of the camera feed

### 4. Colorblindness Simulation
- Three clinical types: Protanopia, Deuteranopia, Tritanopia
- Daltonization matrices from Machado et al. (2009), applied per-frame
- **Correct pipeline**: gamma-decode → matrix → gamma-encode (naive implementations skip this)
- Toggle live with `C` key during camera mode

### 5. Quantitative Benchmark (`color_bench`)
- Evaluates both classifiers on identical 11-class task (fair comparison)
- Reports: category accuracy, mean ΔE2000 error, throughput
- Reproducible: deterministic 80/20 split, fixed RNG seed

---

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
```

Requires: C++17 compiler, CMake ≥ 3.10, OpenCV ≥ 4.x

---

## Usage

```bash
./color_bench    # Run quantitative benchmark
./color_cli      # Interactive CLI: RGB or HEX input → Lab + ΔE2000 output
./color_camera   # Real-time camera analysis
```

### Camera controls
| Key | Action |
|-----|--------|
| `C` | Cycle colorblind simulation (Normal → Protanopia → Deuteranopia → Tritanopia) |
| `M` | Toggle mosaic/pixelation mode |
| `+` / `-` | Adjust mosaic block size |
| `S` | Save screenshot |
| `ESC` | Exit |

---

## Architecture

```
include/
  ColorDetector.h      Original HSV classifier (baseline)
  ColorScience.h       sRGB→XYZ→Lab, ΔE2000, named color palette
  DominantColors.h     K-Means++ dominant color extraction
  ColorblindFilter.h   Daltonization simulation
src/
  ColorDetector.cpp    HSV rule-based classifier
  ColorScience.cpp     CIE colorimetry + CIEDE2000 + XKCD palette
  DominantColors.cpp   K-Means++ in Lab space
  ColorblindFilter.cpp Daltonization with gamma-correct pipeline
  Benchmark.cpp        Evaluation framework (color_bench)
  main.cpp             CLI application
  realtime.cpp         Camera application
```

---

## References

1. Sharma, G., Wu, W., & Dalal, E. N. (2005). The CIEDE2000 Color-Difference Formula.
   *Color Research & Application*, 30(1), 21–30.

2. Machado, G. M., Oliveira, M. M., & Fernandes, L. A. F. (2009). A Physiologically-based
   Model for Simulation of Color Vision Deficiency.
   *IEEE Transactions on Visualization and Computer Graphics*, 15(6), 1291–1298.

3. Arthur, D. & Vassilvitskii, S. (2007). k-means++: The Advantages of Careful Seeding.
   *SODA 2007*, 1027–1035.

4. Monroe, R. (2010). XKCD Color Survey. https://blog.xkcd.com/2010/05/03/color-survey-results/

5. IEC 61966-2-1:1999. Multimedia systems and equipment — Colour measurement and management.
