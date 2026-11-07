"""Generates the gray-box *placeholder* textures in content/textures/.

These are procedural stand-ins so the engine's texture support can be seen and
tested. They are not the game's art: final textures come from the art
direction phase (see the Release 1 plan), made or approved by people.

Usage:  python tools/make_placeholder_textures.py
Needs:  numpy, Pillow
"""
import os

import numpy as np
from PIL import Image

SIZE = 512
OUT = os.path.join(os.path.dirname(__file__), "..", "content", "textures")


def periodic_noise(size, cells, rng):
    """Smooth value noise that tiles seamlessly (wraps at the edges)."""
    grid = rng.random((cells, cells))
    coords = np.arange(size) * cells / size
    i0 = np.floor(coords).astype(int)
    t = coords - i0
    t = t * t * (3 - 2 * t)
    i1 = (i0 + 1) % cells
    a = grid[np.ix_(i0, i0)]
    b = grid[np.ix_(i0, i1)]
    c = grid[np.ix_(i1, i0)]
    d = grid[np.ix_(i1, i1)]
    tx = t[None, :]
    ty = t[:, None]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def fbm(size, base_cells, octaves, rng):
    total = np.zeros((size, size))
    amp, norm, cells = 1.0, 0.0, base_cells
    for _ in range(octaves):
        total += amp * periodic_noise(size, cells, rng)
        norm += amp
        amp *= 0.5
        cells *= 2
    return total / norm


def colorize(t, dark, light):
    dark = np.array(dark, dtype=float)
    light = np.array(light, dtype=float)
    return dark[None, None, :] * (1 - t[..., None]) + light[None, None, :] * t[..., None]


def save(name, rgb):
    os.makedirs(OUT, exist_ok=True)
    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")
    img.save(os.path.join(OUT, name), optimize=True)
    print("wrote", name)


def earth(rng):
    n = fbm(SIZE, 4, 6, rng)
    col = colorize(n, (120, 92, 64), (178, 146, 104))
    # dry grass tint in patches
    patches = fbm(SIZE, 3, 4, rng)
    grassy = np.clip((patches - 0.55) * 4, 0, 1)[..., None]
    col = col * (1 - 0.35 * grassy) + np.array([150, 140, 80]) * 0.35 * grassy
    # small pebbles
    speck = periodic_noise(SIZE, 128, rng)
    pebbles = (speck > 0.93)[..., None]
    col = np.where(pebbles, col * 0.75 + 30, col)
    return col


def sandstone(rng):
    n = fbm(SIZE, 4, 5, rng)
    y = np.linspace(0, 1, SIZE, endpoint=False)[:, None]
    strata = 0.5 + 0.5 * np.sin((y * 9 + n * 0.6) * 2 * np.pi)
    t = 0.55 * n + 0.45 * strata
    return colorize(t, (150, 108, 72), (214, 176, 128))


def mud_plaster(rng):
    n = fbm(SIZE, 3, 6, rng)
    smear = fbm(SIZE, 8, 3, rng)
    t = 0.7 * n + 0.3 * smear
    return colorize(t, (132, 84, 58), (186, 128, 90))


def thatch(rng):
    # Straw runs along the texture's V direction (from roof edge to apex).
    streaks = periodic_noise(SIZE, 160, rng)
    streaks = np.repeat(streaks[:1, :], SIZE, axis=0)  # same along v
    wobble = fbm(SIZE, 6, 3, rng)
    t = np.clip(0.6 * streaks + 0.4 * wobble, 0, 1)
    col = colorize(t, (118, 92, 44), (206, 172, 98))
    bands = 0.85 + 0.15 * np.sin(np.linspace(0, 12 * np.pi, SIZE, endpoint=False))[:, None]
    return col * bands[..., None]


if __name__ == "__main__":
    rng = np.random.default_rng(2026)
    save("earth.png", earth(rng))
    save("sandstone.png", sandstone(rng))
    save("mud_plaster.png", mud_plaster(rng))
    save("thatch.png", thatch(rng))
