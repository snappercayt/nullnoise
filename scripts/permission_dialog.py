#!/usr/bin/env python3
import re
import sys
import xml.etree.ElementTree as ET


ALLOWED_LABELS = {"while using the app", "only this time", "allow"}


def button_center(hierarchy, allowed_labels):
    try:
        root = ET.fromstring(hierarchy)
    except ET.ParseError:
        return None

    for node in root.iter("node"):
        label = node.attrib.get("text", "").strip().lower()
        if label not in allowed_labels:
            continue

        bounds = re.fullmatch(
            r"\[(\d+),(\d+)\]\[(\d+),(\d+)\]",
            node.attrib.get("bounds", ""),
        )
        if bounds:
            left, top, right, bottom = map(int, bounds.groups())
            return (left + right) // 2, (top + bottom) // 2
    return None


def permission_button_center(hierarchy):
    return button_center(hierarchy, ALLOWED_LABELS)


if __name__ == "__main__":
    labels = {label.lower() for label in sys.argv[1:]} or ALLOWED_LABELS
    coordinates = button_center(sys.stdin.read(), labels)
    if coordinates is None:
        raise SystemExit(1)
    print(*coordinates)
