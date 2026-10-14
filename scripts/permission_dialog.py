#!/usr/bin/env python3
import re
import sys
import xml.etree.ElementTree as ET


ALLOWED_LABELS = {"while using the app", "only this time", "allow"}


def permission_button_center(hierarchy):
    try:
        root = ET.fromstring(hierarchy)
    except ET.ParseError:
        return None

    for node in root.iter("node"):
        label = node.attrib.get("text", "").strip().lower()
        if label not in ALLOWED_LABELS:
            continue

        bounds = re.fullmatch(
            r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]",
            node.attrib.get("bounds", ""),
        )
        if bounds:
            left, top, right, bottom = map(int, bounds.groups())
            return (left + right) // 2, (top + bottom) // 2
    return None


if __name__ == "__main__":
    coordinates = permission_button_center(sys.stdin.read())
    if coordinates is None:
        raise SystemExit(1)
    print(*coordinates)
