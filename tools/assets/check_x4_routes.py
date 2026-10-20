#!/usr/bin/env python3
"""Independently check X4 route geometry against the exact displayed chart.

Reads the generated C++ data, not the generator's in-memory paths. The source
image is fitted and classified independently, including the existing median
filter. Coordinates are Q4 chart-pixel centers; pixel cells extend +/-0.5px.
The safe-ocean margin applies to every touched cell center; the continuous
curve's closest distance to land is measured and reported separately.
No game, save, economy, or simulation state is changed.
"""

import argparse
from collections import deque
import json
import math
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageFilter, ImageOps

ROOT = Path(__file__).resolve().parents[2]


def array_body(text, name):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    match = re.search(r"\b" + name + r"\s*\[[^;=]*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError(f"Missing array {name}")
    return match.group(1)


def pairs(text, name):
    return [(int(a), int(b)) for a, b in re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", array_body(text, name))]


def constant(text, name):
    match = re.search(r"\b" + name + r"\s*=\s*(\d+)", text)
    if not match:
        raise ValueError(f"Missing constant {name}")
    return int(match.group(1))


def exact_land(width, height):
    source = Image.open(ROOT / "assets/source/world-map.png").convert("RGB")
    fitted = ImageOps.fit(source, (width, height), method=Image.Resampling.BOX)
    rgb = np.asarray(fitted).astype(int)
    sea = (rgb[:, :, 2] > rgb[:, :, 0] + 10) & (rgb[:, :, 2] >= rgb[:, :, 1])
    image = Image.fromarray((~sea * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(3))
    land = np.asarray(image) > 127
    interior = np.asarray(image.filter(ImageFilter.MinFilter(3))) > 127
    return land, land & ~interior


def verify_chart(coast, width, height):
    header = (ROOT / "img_generated_x4.h").read_text()
    packed = np.array([int(n, 0) for n in re.findall(
        r"0x[0-9a-fA-F]+|\d+", array_body(header, "X4_WORLD_MAP_DATA"))], dtype=np.uint8)
    actual = np.unpackbits(packed).reshape(height, (width + 7) // 8 * 8)[:, :width].astype(bool)
    if not np.array_equal(actual, coast):
        raise ValueError(f"Source-derived mask differs from packed chart at {np.count_nonzero(actual != coast)} pixels")


def ocean_component(land, clearance):
    """Largest 4-connected wrapped ocean with the stated center clearance."""
    height, width = land.shape
    water = ~land.copy()
    radius = math.ceil(clearance)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy >= clearance * clearance - 1e-9:
                continue
            shifted = np.roll(land, dx, axis=1)
            blocked = np.ones_like(land)
            if dy >= 0:
                blocked[dy:] = shifted[:height - dy]
            else:
                blocked[:height + dy] = shifted[-dy:]
            water &= ~blocked
    remaining = water.copy()
    largest = []
    for y, x in zip(*np.nonzero(water)):
        if not remaining[y, x]:
            continue
        remaining[y, x] = False
        queue = deque([(int(x), int(y))])
        component = []
        while queue:
            px, py = queue.popleft()
            component.append((px, py))
            for nx, ny in (((px - 1) % width, py), ((px + 1) % width, py),
                           (px, py - 1), (px, py + 1)):
                if 0 <= ny < height and remaining[ny, nx]:
                    remaining[ny, nx] = False
                    queue.append((nx, ny))
        if len(component) > len(largest):
            largest = component
    return np.asarray(largest)


def segment_check(a, b, land, scale, clearance, allow_start_boundary=False):
    """Closed-cell intersection plus exact distance to nearby land centers."""
    height, width = land.shape
    a = np.asarray(a, dtype=float) / scale
    b = np.asarray(b, dtype=float) / scale
    dx = b[0] - a[0]
    if dx > width / 2:
        b[0] -= width
    elif dx < -width / 2:
        b[0] += width
    delta = b - a
    pad = math.ceil(clearance) + 1
    left = math.floor(min(a[0], b[0])) - pad
    right = math.ceil(max(a[0], b[0])) + pad
    top = max(0, math.floor(min(a[1], b[1])) - pad)
    bottom = min(height - 1, math.ceil(max(a[1], b[1])) + pad)
    yy, xx = np.mgrid[top:bottom + 1, left:right + 1]
    hit = land[yy, xx % width]
    cells = np.column_stack((xx[hit], yy[hit])).astype(float)
    if not len(cells):
        return 0, float("inf")
    lower, upper = np.zeros(len(cells)), np.ones(len(cells))
    for axis in (0, 1):
        if not delta[axis]:
            outside = (a[axis] < cells[:, axis] - 0.5) | (a[axis] > cells[:, axis] + 0.5)
            lower[outside] = 2
        else:
            t0 = (cells[:, axis] - 0.5 - a[axis]) / delta[axis]
            t1 = (cells[:, axis] + 0.5 - a[axis]) / delta[axis]
            lower = np.maximum(lower, np.minimum(t0, t1))
            upper = np.minimum(upper, np.maximum(t0, t1))
    intersections = lower <= upper + 1e-12
    if allow_start_boundary:
        intersections &= upper > 1e-10
    collisions = int(np.count_nonzero(intersections))
    length_squared = float(np.sum(delta * delta))
    t = np.clip(np.sum((cells - a) * delta, axis=1) / length_squared, 0, 1) if length_squared else np.zeros(len(cells))
    distance = np.sqrt(np.sum((cells - a - t[:, None] * delta) ** 2, axis=1))
    return collisions, float(distance.min())


def shoreline_edges(land, raw_ocean, scale):
    """All cardinal land/main-ocean boundaries, with outward unit normals."""
    height, width = land.shape
    ocean = np.zeros_like(land)
    ocean[raw_ocean[:, 1], raw_ocean[:, 0]] = True
    edges = {}
    for y, x in zip(*np.nonzero(land)):
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, ny = (x + dx) % width, y + dy
            if 0 <= ny < height and ocean[ny, nx]:
                point = (int((x * scale + dx * scale // 2) % (width * scale)),
                         int(y * scale + dy * scale // 2))
                edges[point] = (dx, dy)
    return edges


def nearest_entry(shore, normal, safe_ocean, land, scale, maximum=6):
    """Whether an absolute nearest safe point admits a direct water approach.

    A farther entry cannot rescue an unsuitable shoreline edge. The documented
    row-major tie rule matters when one equal-distance entry faces inland.
    """
    width = land.shape[1]
    origin = np.asarray(shore, dtype=float) / scale
    dx = (safe_ocean[:, 0] - origin[0] + width / 2) % width - width / 2
    dy = safe_ocean[:, 1] - origin[1]
    distances = dx * dx + dy * dy
    nearest = float(distances.min())
    if nearest > maximum * maximum + 1e-9:
        return None
    candidates = np.nonzero(np.abs(distances - nearest) < 1e-9)[0]
    for index in sorted(candidates, key=lambda i: (safe_ocean[i, 1], safe_ocean[i, 0]))[:1]:
        if dx[index] * normal[0] + dy[index] * normal[1] <= 0:
            continue
        entry = tuple(int(value) * scale for value in safe_ocean[index])
        collisions, _ = segment_check(shore, entry, land, scale, 0, allow_start_boundary=True)
        if not collisions:
            return math.sqrt(float(distances[index]))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, default=ROOT / "x4_routes.h")
    parser.add_argument("--clearance", type=float, default=3.0,
                        help="Minimum land-center distance of every traversed water-cell center")
    parser.add_argument("--max-anchor-shift", type=float, default=32.0)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    header = args.header.read_text()
    scale, width, height, count = [constant(header, "X4_ROUTE_" + suffix)
                                  for suffix in ("SCALE", "WIDTH", "HEIGHT", "COUNT")]
    if (scale, width, height, count) != (16, 560, 280, 45):
        raise ValueError(f"Unexpected chart geometry: {(scale, width, height, count)}")
    points = pairs(header, "X4_ROUTE_POINTS")
    ports = pairs(header, "X4_ROUTE_PORTS")
    shores = pairs(header, "X4_PORT_SHORES")
    spans = pairs(header, "X4_ROUTE_PATHS")
    distances = [int(n) for n in re.findall(r"\d+", array_body(header, "X4_ROUTE_DISTANCES"))]
    if len(ports) != 24 or len(shores) != 24 or len(spans) != 48 or any(a or b for a, b in spans[count:]):
        raise ValueError("Unexpected port/span count or nonempty unused spans")
    if len(set(shores)) != len(shores):
        raise ValueError("Two port markers occupy the same shore point")
    if len(distances) != len(points):
        raise ValueError("Distance and point arrays differ in length")
    for x, y in points + ports + shores:
        if not (0 <= x < width * scale and 0 <= y < height * scale):
            raise ValueError(f"Point outside chart: {(x, y)}")
    content = (ROOT / "content.cpp").read_text()
    definitions = re.findall(r'\{ "([^"]+)",\s+(\d+),\s+(\d+),', content)[:24]
    lanes = [(int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,[^{}]+\}",
                                                    array_body(content, "LANE_DEFS"))]
    land, coast = exact_land(width, height)
    verify_chart(coast, width, height)
    ocean = ocean_component(land, args.clearance)
    edges = shoreline_edges(land, ocean_component(land, 0), scale)
    edge_points = np.asarray(list(edges))
    eligible_distances = {}
    unsafe = np.ones_like(land)
    unsafe[ocean[:, 1], ocean[:, 0]] = False
    anchors = []
    for index, ((name, raw_x, raw_y), point, shore) in enumerate(zip(definitions, ports, shores)):
        raw = np.array((int(raw_x) * width / 216, int(raw_y) * height / 108))
        actual = np.asarray(point) / scale
        if shore not in edges:
            raise ValueError(f"Port {index} {name}: marker is not a land/main-ocean boundary")
        if point[0] % scale or point[1] % scale or unsafe[point[1] // scale, point[0] // scale]:
            raise ValueError(f"Port {index} {name}: voyage entry is outside the safe ocean")
        approach = actual - np.asarray(shore) / scale
        approach[0] = (approach[0] + width / 2) % width - width / 2
        approach_length = float(np.linalg.norm(approach))
        normal = edges[shore]
        collisions, _ = segment_check(shore, point, land, scale, 0, allow_start_boundary=True)
        if (approach_length > 6 + 1e-9 or collisions or
                approach[0] * normal[0] + approach[1] * normal[1] <= 0):
            raise ValueError(f"Port {index} {name}: invalid {approach_length:.3f}px harbor approach, {collisions} land cells")
        local_distance = nearest_entry(shore, normal, ocean, land, scale)
        if local_distance is None or approach_length > local_distance + 1e-6:
            raise ValueError(f"Port {index} {name}: voyage entry is not nearest reachable safe point to shore")
        shore_delta = np.asarray(shore) / scale - raw
        shore_delta[0] = (shore_delta[0] + width / 2) % width - width / 2
        shore_shift = float(np.linalg.norm(shore_delta))
        # Only inspect candidates closer than the chosen shore; equal-distance
        # tie breaking is deliberately not copied from the generator.
        edge_delta = edge_points / scale - raw
        edge_delta[:, 0] = (edge_delta[:, 0] + width / 2) % width - width / 2
        edge_distances = np.sum(edge_delta * edge_delta, axis=1)
        for candidate in np.nonzero(edge_distances < shore_shift * shore_shift - 1e-6)[0]:
            key = tuple(int(value) for value in edge_points[candidate])
            if key not in eligible_distances:
                eligible_distances[key] = nearest_entry(key, edges[key], ocean, land, scale)
            if eligible_distances[key] is not None:
                raise ValueError(f"Port {index} {name}: a closer eligible shore exists at {key}")
        delta = actual - raw
        delta[0] = (delta[0] + width / 2) % width - width / 2
        displacement = float(np.linalg.norm(delta))
        if displacement > args.max_anchor_shift:
            raise ValueError(f"Port {index} {name}: voyage entry shift {displacement:.3f}px exceeds limit")
        anchors.append({"port": index, "name": name, "shift": round(displacement, 4),
                        "shoreShift": round(shore_shift, 4), "approachLength": round(approach_length, 4),
                        "shoreQ4": shore, "entryQ4": point})
    errors, results, segments = [], [], 0
    for lane, (offset, length) in enumerate(spans[:count]):
        if length < 2 or offset + length > len(points):
            raise ValueError(f"Invalid span for lane {lane}")
        path = points[offset:offset + length]
        cumulative = distances[offset:offset + length]
        a, b = lanes[lane]
        if path[0] != ports[a] or path[-1] != ports[b] or cumulative[0] != 0:
            raise ValueError(f"Wrong endpoints/origin distance for lane {lane}")
        minimum = float("inf")
        tangents = []
        for index, (start, end) in enumerate(zip(path, path[1:]), 1):
            dx = end[0] - start[0]
            dx = (dx + width * scale // 2) % (width * scale) - width * scale // 2
            expected = max(1, math.floor(math.hypot(dx, end[1] - start[1]) + 0.5))
            tangents.append(np.array((dx, end[1] - start[1]), dtype=float))
            if cumulative[index] - cumulative[index - 1] != expected:
                errors.append(f"lane{lane} segment{index}: cumulative length mismatch")
            for p, q in ((start, end), (end, start)):
                collisions, clearance = segment_check(p, q, land, scale, args.clearance)
                unsafe_cells, _ = segment_check(p, q, unsafe, scale, 0)
                minimum = min(minimum, clearance)
                if collisions or unsafe_cells:
                    errors.append(f"lane{lane} segment{index}: {collisions} land cells, {unsafe_cells} unsafe cells, "
                                  f"{clearance:.4f}px continuous clearance")
            segments += 1
        turns = [math.degrees(math.acos(float(np.clip(np.sum(a * b) / max(1, np.linalg.norm(a) * np.linalg.norm(b)), -1, 1))))
                 for a, b in zip(tangents, tangents[1:])]
        straight_dx = (path[-1][0] - path[0][0] + width * scale // 2) % (width * scale) - width * scale // 2
        straight = math.hypot(straight_dx, path[-1][1] - path[0][1])
        results.append({"lane": lane, "from": a, "to": b, "points": length,
                        "minimumClearance": round(minimum, 4), "lengthQ4": cumulative[-1],
                        "maximumTurnDegrees": round(max(turns, default=0), 2),
                        "detourRatio": round(cumulative[-1] / max(1, straight), 3)})
    separation = min(math.hypot((a[0] - b[0] + width * scale / 2) % (width * scale) - width * scale / 2,
                                a[1] - b[1]) / scale
                     for i, a in enumerate(shores) for b in shores[i + 1:])
    report = {"pass": not errors, "lanes": results, "anchors": anchors, "segments": segments,
              "minimumShoreSeparation": round(separation, 4),
              "maximumApproachLength": max(row["approachLength"] for row in anchors),
              "minimumTraversedCellClearance": args.clearance,
              "minimumClearance": min(row["minimumClearance"] for row in results), "errors": errors}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    if errors:
        raise ValueError("\n".join(errors))
    worst = max(anchors, key=lambda row: row["shift"])
    print(f"PASS: {count} lanes, {segments} segments in both directions; exact packed chart mask; "
          f"{args.clearance:g}px traversed-cell clearance, {report['minimumClearance']:.3f}px continuous land-center clearance. "
          f"24 distinct attached shores with nearest local safe entries; max approach {report['maximumApproachLength']:.3f}px, "
          f"minimum shore separation {separation:.3f}px; max entry shift {worst['name']} {worst['shift']:.3f}px.")


if __name__ == "__main__":
    main()
