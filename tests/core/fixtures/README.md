These small, hand-authored WGS84 shapefile fixtures exercise the shared C reader
and Qt renderer with real binary records:

- `multipart_line`: two separate equatorial segments, with no connecting line.
- `polygon_hole`: an outer square from ±20° and a hole from ±5°.
- `dateline_line`: a segment from 179°E/10°S to 179°W/10°N.

Each file has one shape record and a matching SHX index. The fixtures do not need
DBF attributes; the renderer tests geometry independently of feature names.
