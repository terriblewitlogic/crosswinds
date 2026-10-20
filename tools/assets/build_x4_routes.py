#!/usr/bin/env python3
"""Generate water-only X4 display routes from the exact fitted chart land mask.

Requires Pillow, NumPy and SciPy. This changes presentation geometry only:
content ports, lane connectivity, voyage time, risk and economics stay intact.
Run from any directory. --check verifies checked-in outputs without writing.
"""

import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps
from scipy.ndimage import distance_transform_edt, label

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "art-previews"
HEADER = ROOT / "x4_routes.h"
WIDTH, HEIGHT, SCALE = 560, 280, 16
WORLD_WIDTH, WORLD_HEIGHT = 216, 108
CLEARANCE = 3.0
MAX_APPROACH = 6.0
MAX_POINTS = 128
NEIGHBORS = ((1, 0), (0, 1), (-1, 0), (0, -1),
             (1, 1), (-1, 1), (-1, -1), (1, -1))


def read_content():
    text = (ROOT / "content.cpp").read_text()
    ports = [{"name": n, "world": [int(x), int(y)], "region": int(r)}
             for n, x, y, r in re.findall(
                 r'\{ "([^"]+)",\s*(\d+),\s*(\d+),\s*(\d),\s*PORT_', text)]
    block = text.split("const SeaLaneDef LANE_DEFS[MAX_LANES] = {", 1)[1].split("};", 1)[0]
    lanes = [tuple(map(int, row.split(","))) for row in re.findall(r"\{([^{}]+)\}", block)]
    capacity = int(re.search(r"MAX_LANES = (\d+)", (ROOT / "game.h").read_text())[1])
    assert len(ports) == 24 and len(lanes) == 45 and capacity >= len(lanes)
    return ports, lanes, capacity


def chart_land():
    # Deliberately identical to build_generated_art_x4.world_chart, before
    # its MinFilter step extracts the coast. Do not resize the packed outline.
    source = Image.open(ROOT / "assets/source/world-map.png").convert("RGB")
    fitted = ImageOps.fit(source, (WIDTH, HEIGHT), method=Image.Resampling.BOX)
    rgb = np.asarray(fitted).astype(int)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    sea = (b > r + 10) & (b >= g)
    land_image = Image.fromarray((~sea * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(3))
    land = np.asarray(land_image) > 127
    interior = np.asarray(land_image.filter(ImageFilter.MinFilter(3))) > 127
    expected = np.where(land & ~interior, 0, 255).astype(np.uint8)
    actual = np.asarray(Image.open(OUT / "world-map.png").convert("L"))
    if not np.array_equal(expected, actual):
        raise ValueError("Route land mask disagrees with the packed-art coastline preview")
    return land


def largest_wrapped_component(allowed):
    # Four-connected components agree with corner-safe eight-way A* below.
    components, count = label(allowed)
    parent = list(range(count + 1))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for y in range(HEIGHT):
        a, b = int(components[y, 0]), int(components[y, -1])
        if a and b:
            parent[find(a)] = find(b)
    roots = np.array([find(i) for i in range(count + 1)])
    merged = roots[components]
    sizes = np.bincount(merged.ravel(), minlength=count + 1)
    sizes[0] = 0
    return merged == int(sizes.argmax())


def wrapped_delta(a, b, period=WIDTH):
    delta = b - a
    if delta > period / 2:
        delta -= period
    elif delta < -period / 2:
        delta += period
    return delta


def supercover(a, b):
    """All pixel cells touched by a segment; integer coordinates are centers.

    Includes both sides of cell-edge travel and all four cells at a corner.
    Coordinates may be unwrapped; only x wraps, at the 560-pixel chart seam.
    """
    x0, y0 = a
    x1, y1 = b
    dx, dy = x1 - x0, y1 - y0
    sx, sy = x0 + 0.5, y0 + 0.5
    cx, cy = math.floor(sx), math.floor(sy)
    step_x = 1 if dx > 0 else -1 if dx < 0 else 0
    step_y = 1 if dy > 0 else -1 if dy < 0 else 0
    boundary_x = abs(sx - round(sx)) < 1e-9
    boundary_y = abs(sy - round(sy)) < 1e-9
    seen = set()

    def cells(x, y):
        candidates = [(x, y)]
        if not step_x and boundary_x:
            candidates.append((x - 1, y))
        if not step_y and boundary_y:
            candidates.append((x, y - 1))
        if not step_x and not step_y and boundary_x and boundary_y:
            candidates.append((x - 1, y - 1))
        for xx, yy in candidates:
            item = (xx % WIDTH, yy)
            if item not in seen:
                seen.add(item)
                yield item

    yield from cells(cx, cy)
    if boundary_x and step_x:
        yield from cells(cx - 1, cy)
    if boundary_y and step_y:
        yield from cells(cx, cy - 1)
    if boundary_x and boundary_y and step_x and step_y:
        yield from cells(cx - 1, cy - 1)
    tx = ((cx + 1 - sx) / dx if dx > 0 else (sx - cx) / -dx) if dx else math.inf
    ty = ((cy + 1 - sy) / dy if dy > 0 else (sy - cy) / -dy) if dy else math.inf
    delta_x = 1 / abs(dx) if dx else math.inf
    delta_y = 1 / abs(dy) if dy else math.inf
    while min(tx, ty) <= 1 + 1e-9:
        if abs(tx - ty) < 1e-9:
            yield from cells(cx + step_x, cy)
            yield from cells(cx, cy + step_y)
            cx += step_x
            cy += step_y
            tx += delta_x
            ty += delta_y
        elif tx < ty:
            cx += step_x
            tx += delta_x
        else:
            cy += step_y
            ty += delta_y
        yield from cells(cx, cy)


def segment_clear(a, b, allowed):
    return all(0 <= y < HEIGHT and allowed[y, x] for x, y in supercover(a, b))


def unwrap(points):
    result = [tuple(points[0])]
    for x, y in points[1:]:
        previous = result[-1][0]
        result.append((previous + wrapped_delta(previous % WIDTH, x % WIDTH), y))
    return result


def nearest_pixel(point, allowed):
    ys, xs = np.where(allowed)
    dx = np.abs(xs - point[0])
    dx = np.minimum(dx, WIDTH - dx)
    distances = dx * dx + (ys - point[1]) ** 2
    index = int(distances.argmin())
    return (int(xs[index]), int(ys[index])), float(math.sqrt(distances[index]))


def ocean_shores(land, ocean):
    shores = []
    for y, x in zip(*np.where(ocean)):
        x, y = int(x), int(y)
        for dx, dy in NEIGHBORS[:4]:
            xx, yy = (x + dx) % WIDTH, y + dy
            if 0 <= yy < HEIGHT and land[yy, xx]:
                shores.append(((x + dx * 0.5) % WIDTH, y + dy * 0.5,
                               (x, y), (xx, yy)))
    return shores


def shore_approach_clear(shore, entry, ocean):
    x, y, water_cell, land_cell = shore
    target = (x + wrapped_delta(x, entry[0]), entry[1])
    outward = (wrapped_delta(x, water_cell[0]), water_cell[1] - y)
    if (target[0] - x) * outward[0] + (target[1] - y) * outward[1] <= 0:
        return False
    # The exact shoreline endpoint touches its one neighboring land cell.
    # Every positive-length part of the connector must face and occupy water.
    return all(0 <= yy < HEIGHT and (ocean[yy, xx] or (xx, yy) == land_cell)
               for xx, yy in supercover((x, y), target))


def select_shore(raw, shores, allowed, ocean, used_shores, used_entries, cache):
    ordered = sorted(shores, key=lambda s: (wrapped_delta(raw[0], s[0]) ** 2 +
                                            (raw[1] - s[1]) ** 2, s[1], s[0], s[2]))
    for shore in ordered:
        point = shore[:2]
        if point in used_shores:
            continue
        if point not in cache:
            entry, distance = nearest_pixel(point, allowed)
            cache[point] = (entry, distance)
        entry, distance = cache[point]
        if distance > MAX_APPROACH or entry in used_entries:
            continue
        if not shore_approach_clear(shore, entry, ocean):
            continue
        return shore, entry, distance
    raise ValueError(f"No distinct ocean-facing shore and short approach near {raw}")


def find_path(start, finish, allowed, clearance):
    def heuristic(x, y):
        dx, dy = abs(wrapped_delta(x, finish[0])), abs(y - finish[1])
        return max(dx, dy) + (math.sqrt(2) - 1) * min(dx, dy)

    costs = np.full((HEIGHT, WIDTH), math.inf)
    previous = np.full((HEIGHT, WIDTH), -1, dtype=np.int32)
    costs[start[1], start[0]] = 0
    queue = [(heuristic(*start), 0.0, start[1] * WIDTH + start[0])]
    while queue:
        _, cost, index = heapq.heappop(queue)
        y, x = divmod(index, WIDTH)
        if cost > costs[y, x] + 1e-9:
            continue
        if (x, y) == finish:
            points = []
            while index >= 0:
                yy, xx = divmod(index, WIDTH)
                points.append((xx, yy))
                index = int(previous[yy, xx])
            return unwrap(points[::-1])
        for dx, dy in NEIGHBORS:
            xx, yy = (x + dx) % WIDTH, y + dy
            if not 0 <= yy < HEIGHT or not allowed[yy, xx]:
                continue
            if dx and dy and (not allowed[y, xx] or not allowed[yy, x]):
                continue
            # Favor room to round a turn offshore, without changing connectivity.
            penalty = 1 + 2 * math.exp(-min(clearance[y, x], clearance[yy, xx]) / 4)
            new = cost + (math.sqrt(2) if dx and dy else 1) * penalty
            if new + 1e-9 < costs[yy, xx]:
                costs[yy, xx] = new
                previous[yy, xx] = index
                heapq.heappush(queue, (new + heuristic(xx, yy), new, yy * WIDTH + xx))
    raise ValueError(f"No offshore route from {start} to {finish}")


def simplify(points, allowed):
    result, current = [points[0]], 0
    while current < len(points) - 1:
        end = len(points) - 1
        while end > current + 1 and not segment_clear(points[current], points[end], allowed):
            end -= 1
        result.append(points[end])
        current = end
    return result


def rounded_path(points, allowed):
    result = [points[0]]
    for i in range(1, len(points) - 1):
        a, b, c = np.array(points[i - 1]), np.array(points[i]), np.array(points[i + 1])
        length_a, length_c = float(np.linalg.norm(a - b)), float(np.linalg.norm(c - b))
        cut = min(12.0, length_a * 0.3, length_c * 0.3)
        curve = None
        while cut >= 0.125:
            entry, leave = b + (a - b) * cut / length_a, b + (c - b) * cut / length_c
            count = max(2, min(12, math.ceil(math.sqrt(cut / 0.12))))
            candidate = [tuple((1 - t) ** 2 * entry + 2 * t * (1 - t) * b + t * t * leave)
                         for t in np.linspace(0, 1, count + 1)]
            check = [result[-1]] + candidate + [tuple(c)]
            if (all(segment_clear(p, q, allowed) for p, q in zip(check, check[1:])) and
                    quantized_clear(quantize(check), allowed)):
                curve = candidate
                break
            cut /= 2
        result.extend(curve if curve else [tuple(b)])
    result.append(points[-1])
    return result


def quantize(points):
    result = []
    for x, y in points:
        xx = int(math.floor(x * SCALE + 0.5)) % (WIDTH * SCALE)
        yy = int(math.floor(y * SCALE + 0.5))
        point = (xx, yy)
        if not result or point != result[-1]:
            result.append(point)
    return result


def quantized_clear(points, allowed):
    floating = unwrap([(x / SCALE, y / SCALE) for x, y in points])
    return all(segment_clear(a, b, allowed) for a, b in zip(floating, floating[1:]))


def smooth_quantized(points, allowed):
    curved = rounded_path(points, allowed)
    quantized = quantize(curved)
    if quantized_clear(quantized, allowed) and len(quantized) <= MAX_POINTS:
        return quantized, True
    # Quantization may move a grazing curve across a cell corner. Keep the
    # already validated polyline rather than changing or weakening clearance.
    quantized = quantize(points)
    if not quantized_clear(quantized, allowed) or len(quantized) > MAX_POINTS:
        raise ValueError("Quantized route failed clearance or point budget")
    return quantized, False


def cumulative_distances(points):
    result = [0]
    for a, b in zip(points, points[1:]):
        dx = wrapped_delta(a[0], b[0], WIDTH * SCALE)
        distance = max(1, int(math.floor(math.hypot(dx, b[1] - a[1]) + 0.5)))
        result.append(result[-1] + distance)
    if result[-1] >= 65536:
        raise ValueError("Route cumulative distance exceeds uint16_t")
    return result


def collision_summary(points, land):
    points = unwrap(points)
    touched, collisions = set(), set()
    for a, b in zip(points, points[1:]):
        for x, y in supercover(a, b):
            if not 0 <= y < HEIGHT:
                raise ValueError("Route crosses the top or bottom chart edge")
            touched.add((x, y))
            if land[y, x]:
                collisions.add((x, y))
    return touched, collisions


def draw_routes(land, paths, anchors, shores, colors=True):
    palette = ((32, 98, 166), (187, 75, 20), (44, 127, 91),
               (133, 71, 146), (35, 132, 145), (153, 111, 28))
    background = np.where(land[..., None], np.array([211, 215, 204]), np.array([251, 253, 255]))
    image = Image.fromarray(background.astype(np.uint8)).resize((WIDTH * 3, HEIGHT * 3), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for i, path in enumerate(paths):
        color = palette[i % len(palette)] if colors else (20, 20, 20)
        points = unwrap([(x / SCALE, y / SCALE) for x, y in path])
        for a, b in zip(points, points[1:]):
            for shift in (-WIDTH, 0, WIDTH):
                draw.line(((a[0] + shift) * 3, a[1] * 3, (b[0] + shift) * 3, b[1] * 3), fill=color, width=2)
    for i, ((x, y), anchor) in enumerate(zip(shores, anchors)):
        endpoint_x = x + wrapped_delta(x, anchor[0])
        for shift in (-WIDTH, 0, WIDTH):
            draw.line(((x + shift) * 3, y * 3, (endpoint_x + shift) * 3, anchor[1] * 3),
                      fill=(45, 45, 45), width=2)
        draw.ellipse((anchor[0] * 3 - 2, anchor[1] * 3 - 2,
                      anchor[0] * 3 + 2, anchor[1] * 3 + 2), fill=(45, 45, 45))
        x, y = x * 3, y * 3
        draw.ellipse((x - 4, y - 4, x + 4, y + 4), fill=(255, 255, 255), outline=(0, 0, 0), width=2)
        draw.text((x + 6, y - 6), str(i), fill=(0, 0, 0))
    return image


def generate():
    ports, lanes, capacity = read_content()
    land = chart_land()
    ocean = largest_wrapped_component(~land)
    # Tiling includes land just across the horizontal seam in distance queries.
    distance = distance_transform_edt(np.tile(~land, (1, 3)))[:, WIDTH:WIDTH * 2]
    allowed = largest_wrapped_component(ocean & (distance >= CLEARANCE))
    candidates = ocean_shores(land, ocean)
    anchors, shores, port_records = [], [], []
    used_shores, used_entries, shore_cache = set(), set(), {}
    for index, port in enumerate(ports):
        raw = (port["world"][0] * WIDTH / WORLD_WIDTH, port["world"][1] * HEIGHT / WORLD_HEIGHT)
        nearest_water, water_shift = nearest_pixel(raw, ocean)
        edge, anchor, approach_length = select_shore(raw, candidates, allowed, ocean,
                                                   used_shores, used_entries, shore_cache)
        shore = edge[:2]
        shift = math.hypot(wrapped_delta(raw[0], anchor[0]), raw[1] - anchor[1])
        shore_shift = math.hypot(wrapped_delta(raw[0], shore[0]), raw[1] - shore[1])
        anchors.append(anchor)
        shores.append(shore)
        used_shores.add(shore)
        used_entries.add(anchor)
        port_records.append(dict(index=index, **port, originalChart=list(raw),
                                 originalOnLand=bool(land[round(raw[1]), round(raw[0]) % WIDTH]),
                                 nearestOceanPixel=list(nearest_water), nearestOceanDistancePx=round(water_shift, 6),
                                 anchorChart=list(anchor), anchorQ4=[v * SCALE for v in anchor],
                                 displacementPx=round(shift, 6), clearancePx=round(float(distance[anchor[1], anchor[0]]), 6),
                                 shoreChart=list(shore), shoreQ4=[int(v * SCALE) for v in shore],
                                 shoreDisplacementPx=round(shore_shift, 6), shoreOceanPixel=list(edge[2]),
                                 shoreLandPixel=list(edge[3]), approachLengthPx=round(approach_length, 6),
                                 approachWaterOnly=True))
    assert len(used_shores) == len(ports) and len(used_entries) == len(ports)
    baseline = json.loads((ROOT / "assets/source/x4/routes-baseline.json").read_text())
    baseline_records = []
    paths, distances, route_records = [], [], []
    all_original_collision_pixels = set()
    for index, lane in enumerate(lanes):
        a, b = lane[:2]
        old = [ports[a]["world"]] + baseline["paths"][index] + [ports[b]["world"]]
        old = [(x * WIDTH / WORLD_WIDTH, y * HEIGHT / WORLD_HEIGHT) for x, y in old]
        _, intersections = collision_summary(old, land)
        all_original_collision_pixels.update(intersections)
        baseline_records.append({"lane": index, "landPixelsTouched": len(intersections)})
        grid_path = find_path(anchors[a], anchors[b], allowed, distance)
        simple = simplify(grid_path, allowed)
        path, rounded = smooth_quantized(simple, allowed)
        length = cumulative_distances(path)
        floating = [(x / SCALE, y / SCALE) for x, y in path]
        touched, intersections = collision_summary(floating, land)
        reverse_touched, reverse_intersections = collision_summary(floating[::-1], land)
        assert not intersections and not reverse_intersections and touched == reverse_touched
        assert quantized_clear(path, allowed) and quantized_clear(path[::-1], allowed)
        assert path[0] == tuple(v * SCALE for v in anchors[a])
        assert path[-1] == tuple(v * SCALE for v in anchors[b])
        assert all(0 <= x < WIDTH * SCALE and 0 <= y <= (HEIGHT - 1) * SCALE for x, y in path)
        paths.append(path)
        distances.append(length)
        route_records.append({"lane": index, "a": a, "b": b, "points": len(path),
                              "rounded": rounded, "lengthQ4": length[-1],
                              "minTouchedPixelClearancePx": round(min(float(distance[y, x]) for x, y in touched), 6),
                              "crossesSeam": any(abs(p[0] - q[0]) > WIDTH * SCALE / 2 for p, q in zip(path, path[1:])),
                              "landIntersections": 0, "reverseMatches": True})
        print(f"lane {index:02d} {ports[a]['name']} -> {ports[b]['name']}: {len(path)} points", flush=True)
    flat = [point for path in paths for point in path]
    flat_distances = [value for values in distances for value in values]
    if len(flat) >= 65536:
        raise ValueError("Flat route offsets exceed uint16_t")
    spans, offset = [], 0
    for path in paths:
        spans.append((offset, len(path)))
        offset += len(path)
    spans += [(0, 0)] * (capacity - len(spans))
    packed = b"".join(struct.pack("<HH", x * SCALE, y * SCALE) for x, y in anchors)
    packed += b"".join(struct.pack("<HH", int(x * SCALE), int(y * SCALE)) for x, y in shores)
    packed += b"".join(struct.pack("<HH", *p) for p in flat)
    packed += b"".join(struct.pack("<H", d) for d in flat_distances)
    packed += b"".join(struct.pack("<HH", *span) for span in spans)
    header = ["// Generated by tools/assets/build_x4_routes.py. Do not edit by hand.",
              "// Cosmetic routes only; content ports, sea lanes and simulation are unchanged.",
              "// Q4 chart-pixel centers; x wraps at WIDTH*SCALE. Pixel cells extend +/-SCALE/2.",
              "// Distances sum each shortest-wrapped segment length rounded nearest (ties up).",
              "#pragma once", "#include <stdint.h>", "#ifndef PROGMEM", "#define PROGMEM", "#endif", "",
              f"static constexpr uint16_t X4_ROUTE_SCALE = {SCALE};",
              f"static constexpr uint16_t X4_ROUTE_WIDTH = {WIDTH};",
              f"static constexpr uint16_t X4_ROUTE_HEIGHT = {HEIGHT};",
              f"static constexpr uint16_t X4_ROUTE_COUNT = {len(lanes)};",
              f"static constexpr uint16_t X4_ROUTE_POINT_COUNT = {len(flat)};",
              f"static constexpr uint32_t X4_ROUTE_PACKED_BYTES = {len(packed)}u;",
              "struct X4RoutePoint { uint16_t x, y; };",
              "struct X4RouteSpan { uint16_t offset, count; };", "",
              f"static const X4RoutePoint X4_ROUTE_PORTS[{len(anchors)}] PROGMEM = {{"]
    header += [f"  {{{x * SCALE}, {y * SCALE}}}, // {ports[i]['name']}" for i, (x, y) in enumerate(anchors)]
    header += ["};", "", "// Coast-edge markers; routes and sailing ships use X4_ROUTE_PORTS above.",
               f"static const X4RoutePoint X4_PORT_SHORES[{len(shores)}] PROGMEM = {{"]
    header += [f"  {{{int(x * SCALE)}, {int(y * SCALE)}}}, // {ports[i]['name']}" for i, (x, y) in enumerate(shores)]
    header += ["};", "", f"static const X4RoutePoint X4_ROUTE_POINTS[{len(flat)}] PROGMEM = {{"]
    for i, path in enumerate(paths):
        header.append(f"  // Lane {i}: {ports[lanes[i][0]]['name']} -> {ports[lanes[i][1]]['name']}")
        header += ["  " + ", ".join(f"{{{x}, {y}}}" for x, y in path[j:j + 8]) + "," for j in range(0, len(path), 8)]
    header += ["};", "", f"static const uint16_t X4_ROUTE_DISTANCES[{len(flat)}] PROGMEM = {{"]
    for i, values in enumerate(distances):
        header.append(f"  // Lane {i}")
        header += ["  " + ", ".join(map(str, values[j:j + 16])) + "," for j in range(0, len(values), 16)]
    header += ["};", "", f"static const X4RouteSpan X4_ROUTE_PATHS[{capacity}] PROGMEM = {{"]
    header += [f"  {{{offset}, {count}}}," for offset, count in spans]
    header += ["};", "", "static_assert(sizeof(X4RoutePoint) == 4 && sizeof(X4RouteSpan) == 4, \"Route packing changed\");", ""]
    manifest = {"description": "Water-only cosmetic X4 routes; no economic or content changes",
                "source": "assets/source/world-map.png", "sourceSha256": hashlib.sha256((ROOT / "assets/source/world-map.png").read_bytes()).hexdigest(),
                "chartSize": [WIDTH, HEIGHT], "worldSize": [WORLD_WIDTH, WORLD_HEIGHT], "scale": SCALE,
                "landMaskSha256": hashlib.sha256(np.packbits(land, bitorder="big").tobytes()).hexdigest(),
                "landMaskRule": "ImageOps.fit RGB BOX to560x280; sea=(b>r+10)&(b>=g); landMedianFilter3",
                "coordinateConvention": "integer chart coordinates are pixel centers; Q4 cells extend +/-8; x wraps at8960",
                "clearanceRule": "all supercover cells of every final Q4 segment have >=3px center-to-land-pixel-center distance; anchors belong to largest wrapped offshore ocean component",
                "targetClearancePx": CLEARANCE, "waterPixels": int(ocean.sum()), "offshoreOceanPixels": int(allowed.sum()),
                "shoreSelectionRule": "nearest distinct main-ocean/land edge to original content port with a <=6px straight water-only approach to its nearest distinct 3px-safe ocean entry; only the exact shore endpoint may touch its neighboring land cell",
                "maxApproachPx": MAX_APPROACH,
                "pointCount": len(flat), "maxPointsPerLane": max(map(len, paths)), "packedBytes": len(packed),
                "packedSha256": hashlib.sha256(packed).hexdigest(), "ports": port_records, "routes": route_records,
                "baseline": {"source": "assets/source/x4/routes-baseline.json", "lanesTouchingLand": sum(r["landPixelsTouched"] > 0 for r in baseline_records),
                             "uniqueLandPixelsTouched": len(all_original_collision_pixels), "routes": baseline_records},
                "validation": {"lanes": len(paths), "directions": len(paths) * 2, "landIntersections": 0,
                               "allAnchorsInOcean": True, "allEndpointsMatchAnchors": True,
                               "allShoresOnMainOceanEdges": True, "allApproachesWaterOnly": True,
                               "allApproachesAtMost6px": True, "allShoresDistinct": True, "allEntriesDistinct": True,
                               "allQuantizedSegmentsRetainClearance": True, "reverseSupercoversMatch": True,
                               "seamLanes": [r["lane"] for r in route_records if r["crossesSeam"]]}}
    return "\n".join(header), json.dumps(manifest, indent=2) + "\n", land, paths, anchors, shores


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    header, manifest, land, paths, anchors, shores = generate()
    outputs = ((HEADER, header), (OUT / "routes-manifest.json", manifest))
    if args.check:
        for path, text in outputs:
            if not path.is_file() or path.read_text() != text:
                raise SystemExit(f"Generated route output is stale: {path.relative_to(ROOT)}")
        print("PASS: route outputs are deterministic and current; 45 routes valid in both directions")
        return
    for path, text in outputs:
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(text)
        temporary.replace(path)
    OUT.mkdir(parents=True, exist_ok=True)
    draw_routes(land, paths, anchors, shores).save(OUT / "routes-overview.png", optimize=True)
    for region in range(6):
        selected = [path for path, row in zip(paths, json.loads(manifest)["routes"])
                    if row["a"] // 4 == region or row["b"] // 4 == region]
        draw_routes(land, selected, anchors, shores).save(OUT / f"routes-region-{region}.png", optimize=True)
    Image.fromarray(np.where(land, 0, 255).astype(np.uint8)).save(OUT / "routes-land-mask.png", optimize=True)
    print(f"Wrote {HEADER.relative_to(ROOT)} and routes QA assets")


if __name__ == "__main__":
    main()
