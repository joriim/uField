# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only feature tools on a project's data.gpkg: list_layers, query_features.

Used by ufield-mcp so Claude can see the observations a user collected in the
app (release 1, ADR 0007). Everything here reads; nothing writes data.gpkg.

- Geometries are decoded from GeoPackage binary (header + WKB) and returned as
  GeoJSON in EPSG:4326 (CLAUDE.md, CRS rule), reprojected with pyproj.
- `filter` accepts a safe subset of QGIS expressions, translated to SQL with
  parameters: comparisons, AND/OR/NOT, parentheses, IS [NOT] NULL,
  [NOT] LIKE, [NOT] IN (...). Field names are checked against the layer.
- Project layer ids are table names; a name with characters outside the
  schema's id pattern gets an ASCII id (näytteet -> naytteet).
"""

from __future__ import annotations

import math
import re
import sqlite3
import struct
from datetime import date, datetime, timezone
from functools import lru_cache
from pathlib import Path

import yaml

from . import schema as us
from .dispatcher import CallContext, ToolDispatcher, ToolError

_ID_OK = re.compile(r"^[a-zA-Z0-9_-]{1,64}$")
_TRANSLIT = str.maketrans({"ä": "a", "ö": "o", "å": "a", "Ä": "A", "Ö": "O", "Å": "A", "é": "e", "ü": "u"})
TIME_FIELDS = ("ufield_created", "created", "datetime", "timestamp", "date")
EARTH_RADIUS_M = 6371008.8


# -- layer ids ---------------------------------------------------------------


def layer_ids(tables: list[str]) -> dict[str, str]:
    """layer_id -> table name, ASCII ids for tables whose names don't fit the id pattern."""
    out: dict[str, str] = {}
    for table in tables:
        base = table if _ID_OK.match(table) else (re.sub(r"[^a-zA-Z0-9_-]+", "_", table.translate(_TRANSLIT)).strip("_")[:60] or "layer")
        lid, n = base, 2
        while lid in out:
            lid, n = f"{base}_{n}", n + 1
        out[lid] = table
    return out


def _q(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


# -- GeoPackage geometry -------------------------------------------------------

_ENVELOPE_BYTES = {0: 0, 1: 32, 2: 48, 3: 48, 4: 64}


def decode_gpkg_geometry(blob: bytes | None) -> dict | None:
    """GeoPackage binary geometry -> GeoJSON geometry (coordinates as stored)."""
    if blob is None:
        return None
    if len(blob) < 8 or blob[:2] != b"GP":
        raise ValueError("not a GeoPackage geometry")
    flags = blob[3]
    if flags & 0x10:  # empty geometry
        return None
    env = (flags >> 1) & 0x07
    if env not in _ENVELOPE_BYTES:
        raise ValueError("invalid envelope indicator")
    geom, _ = _read_wkb(blob, 8 + _ENVELOPE_BYTES[env])
    return geom


def _read_wkb(buf: bytes, pos: int) -> tuple[dict, int]:
    order = "<" if buf[pos] == 1 else ">"
    (code,) = struct.unpack_from(order + "I", buf, pos + 1)
    pos += 5
    has_z = bool(code & 0x80000000)
    has_m = bool(code & 0x40000000)
    if code & 0x20000000:  # EWKB SRID
        pos += 4
    code &= 0x0FFFFFFF
    if code >= 1000:
        dim = code // 1000
        code %= 1000
        has_z = has_z or dim in (1, 3)
        has_m = has_m or dim in (2, 3)
    n = 2 + has_z + has_m

    def point(p: int) -> tuple[list[float], int]:
        vals = struct.unpack_from(order + "d" * n, buf, p)
        return list(vals[: 3 if has_z else 2]), p + 8 * n

    def ring(p: int) -> tuple[list, int]:
        (count,) = struct.unpack_from(order + "I", buf, p)
        p += 4
        pts = []
        for _ in range(count):
            pt, p = point(p)
            pts.append(pt)
        return pts, p

    if code == 1:
        coords, pos = point(pos)
        if all(math.isnan(c) for c in coords):
            return None, pos
        return {"type": "Point", "coordinates": coords}, pos
    if code == 2:
        coords, pos = ring(pos)
        return {"type": "LineString", "coordinates": coords}, pos
    if code == 3:
        (count,) = struct.unpack_from(order + "I", buf, pos)
        pos += 4
        rings = []
        for _ in range(count):
            r, pos = ring(pos)
            rings.append(r)
        return {"type": "Polygon", "coordinates": rings}, pos
    if code in (4, 5, 6, 7):
        (count,) = struct.unpack_from(order + "I", buf, pos)
        pos += 4
        parts = []
        for _ in range(count):
            part, pos = _read_wkb(buf, pos)
            parts.append(part)
        if code == 7:
            return {"type": "GeometryCollection", "geometries": [p for p in parts if p]}, pos
        kind = {4: "MultiPoint", 5: "MultiLineString", 6: "MultiPolygon"}[code]
        return {"type": kind, "coordinates": [p["coordinates"] for p in parts if p]}, pos
    raise ValueError(f"unsupported WKB geometry type {code}")


def _map_coords(geom: dict | None, fn) -> dict | None:
    if geom is None:
        return None
    if geom["type"] == "GeometryCollection":
        return {"type": "GeometryCollection", "geometries": [_map_coords(g, fn) for g in geom["geometries"]]}

    def walk(c):
        return fn(c) if c and isinstance(c[0], (int, float)) else [walk(x) for x in c]

    return {"type": geom["type"], "coordinates": walk(geom["coordinates"])}


def _positions(geom: dict | None):
    if geom is None:
        return
    if geom["type"] == "GeometryCollection":
        for g in geom["geometries"]:
            yield from _positions(g)
        return

    def walk(c):
        if c and isinstance(c[0], (int, float)):
            yield c
        else:
            for x in c:
                yield from walk(x)

    yield from walk(geom["coordinates"])


@lru_cache(maxsize=16)
def _transformer(srs: str):
    from pyproj import Transformer

    return Transformer.from_crs(srs, "EPSG:4326", always_xy=True)


def to_wgs84(geom: dict | None, srs: str | None) -> dict | None:
    if geom is None or srs in (None, "EPSG:4326"):
        return geom
    t = _transformer(srs)

    def fn(c):
        x, y = t.transform(c[0], c[1])
        return [round(x, 8), round(y, 8)] + ([c[2]] if len(c) > 2 else [])

    return _map_coords(geom, fn)


def _bbox_of(geom: dict | None) -> tuple[float, float, float, float] | None:
    pts = list(_positions(geom))
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def haversine_m(lon1: float, lat1: float, lon2: float, lat2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(min(1.0, math.sqrt(a)))


# -- filter expressions --------------------------------------------------------

_TOKEN = re.compile(
    r"""\s*(?:
      (?P<num>-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)
    | '(?P<str>(?:[^']|'')*)'
    | "(?P<qid>(?:[^"]|"")+)"
    | (?P<op><=|>=|<>|!=|=|<|>|\(|\)|,)
    | (?P<word>[^\W\d][\w]*)
    )""",
    re.VERBOSE | re.UNICODE,
)
_KEYWORDS = {"AND", "OR", "NOT", "IS", "NULL", "LIKE", "ILIKE", "IN", "TRUE", "FALSE"}


class FilterError(ValueError):
    pass


def compile_filter(expr: str, columns: set[str]) -> tuple[str, list]:
    """A QGIS-expression subset -> (SQL WHERE clause, parameters)."""
    tokens, pos = [], 0
    while pos < len(expr):
        if expr[pos:].strip() == "":
            break
        m = _TOKEN.match(expr, pos)
        if not m or m.end() == pos:
            raise FilterError(f"cannot read the filter at: {expr[pos:pos + 20]!r}")
        pos = m.end()
        if m.group("num") is not None:
            tokens.append(("lit", float(m.group("num")) if re.search(r"[.eE]", m.group("num")) else int(m.group("num"))))
        elif m.group("str") is not None:
            tokens.append(("lit", m.group("str").replace("''", "'")))
        elif m.group("qid") is not None:
            tokens.append(("id", m.group("qid").replace('""', '"')))
        elif m.group("op") is not None:
            tokens.append(("op", m.group("op")))
        else:
            word = m.group("word")
            if word.upper() in _KEYWORDS:
                tokens.append(("kw", word.upper()))
            else:
                tokens.append(("id", word))
    params: list = []
    i = 0

    def peek(kind=None, value=None):
        if i >= len(tokens):
            return None
        t = tokens[i]
        if kind and t[0] != kind:
            return None
        if value and t[1] != value:
            return None
        return t

    def take(kind=None, value=None):
        nonlocal i
        t = peek(kind, value)
        if t is None:
            want = value or kind or "more input"
            got = tokens[i][1] if i < len(tokens) else "end of filter"
            raise FilterError(f"expected {want}, found {got!r}")
        i += 1
        return t

    def operand() -> str:
        t = peek()
        if t is None:
            raise FilterError("the filter ends too early")
        if t[0] == "id":
            take()
            if t[1] not in columns:
                raise FilterError(f"no field named {t[1]!r} in this layer")
            return _q(t[1])
        if t[0] == "lit":
            take()
            params.append(t[1])
            return "?"
        if t[0] == "kw" and t[1] in ("TRUE", "FALSE"):
            take()
            params.append(1 if t[1] == "TRUE" else 0)
            return "?"
        if t[0] == "kw" and t[1] == "NULL":
            take()
            return "NULL"
        raise FilterError(f"expected a field or a value, found {t[1]!r}")

    def comparison() -> str:
        if peek("op", "("):
            take()
            inner = disjunction()
            take("op", ")")
            return f"({inner})"
        left = operand()
        if peek("kw", "IS"):
            take()
            neg = bool(peek("kw", "NOT")) and take()
            take("kw", "NULL")
            return f"{left} IS {'NOT ' if neg else ''}NULL"
        neg = bool(peek("kw", "NOT")) and take()
        if peek("kw", "LIKE") or peek("kw", "ILIKE"):
            take()
            right = operand()
            return f"{left} {'NOT ' if neg else ''}LIKE {right}"
        if peek("kw", "IN"):
            take()
            take("op", "(")
            items = [operand()]
            while peek("op", ","):
                take()
                items.append(operand())
            take("op", ")")
            return f"{left} {'NOT ' if neg else ''}IN ({', '.join(items)})"
        if neg:
            raise FilterError("NOT here must be followed by LIKE or IN")
        t = peek("op")
        if t and t[1] in ("=", "!=", "<>", "<", "<=", ">", ">="):
            take()
            right = operand()
            return f"{left} {'<>' if t[1] == '!=' else t[1]} {right}"
        return left  # a bare field or value, used as a truth value

    def negation() -> str:
        if peek("kw", "NOT"):
            take()
            return f"NOT ({negation()})"
        return comparison()

    def conjunction() -> str:
        out = negation()
        while peek("kw", "AND"):
            take()
            out = f"{out} AND {negation()}"
        return out

    def disjunction() -> str:
        out = conjunction()
        while peek("kw", "OR"):
            take()
            out = f"{out} OR {conjunction()}"
        return out

    sql = disjunction()
    if i != len(tokens):
        raise FilterError(f"unexpected {tokens[i][1]!r} in the filter")
    return sql, params


# -- registry ------------------------------------------------------------------


def load_registry_layers(path: Path = us.LAYERS_PATH) -> list[dict]:
    try:
        reg = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except OSError:
        return []
    providers = reg.get("providers", {})
    out = []
    for layer in reg.get("layers", []):
        p = providers.get(layer.get("provider"), {})
        out.append(
            {
                "layer_id": layer["id"],
                "title": layer.get("title", layer["id"]),
                "source": "registry",
                "provider": p.get("name", layer.get("provider")),
                "category": layer.get("category"),
                "service_type": layer.get("type"),
                "license": layer.get("license", p.get("license")),
                "attribution": layer.get("attribution", p.get("attribution")),
                "needs_credentials": layer.get("auth", "none") != "none",
            }
        )
    return out


# -- the tools -----------------------------------------------------------------


def _jsonable(value):
    if isinstance(value, bytes):
        return None
    if isinstance(value, (datetime, date)):
        return value.isoformat()
    if isinstance(value, float) and (math.isnan(value) or math.isinf(value)):
        return None
    return value


def _parse_time(value) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)


class ProjectFeatures:
    def __init__(self, project, registry_layers: list[dict] | None = None):
        self.project = project
        self.registry_layers = load_registry_layers() if registry_layers is None else registry_layers

    def _connect(self) -> sqlite3.Connection:
        self.project.require_project()
        try:
            return sqlite3.connect(f"{self.project.gpkg.as_uri()}?mode=ro", uri=True)
        except sqlite3.DatabaseError as e:
            raise ToolError("project_unreadable", f"cannot read data.gpkg: {e}") from e

    def _layers(self, con: sqlite3.Connection) -> list[dict]:
        try:
            contents = con.execute(
                "SELECT table_name, data_type, identifier FROM gpkg_contents WHERE data_type IN ('features', 'attributes')"
            ).fetchall()
        except sqlite3.DatabaseError:
            contents = [(r[0], r[1], None) for r in con.execute("SELECT table_name, data_type FROM gpkg_contents WHERE data_type IN ('features', 'attributes')")]
        existing = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type IN ('table', 'view')")}
        contents = [c for c in contents if c[0] in existing]  # skip stale gpkg_contents rows
        geom_cols = {}
        try:
            for table, column, gtype, srs_id in con.execute(
                "SELECT table_name, column_name, geometry_type_name, srs_id FROM gpkg_geometry_columns"
            ):
                geom_cols[table] = (column, gtype, srs_id)
        except sqlite3.DatabaseError:
            pass
        srs_names = {}
        try:
            for srs_id, org, code in con.execute("SELECT srs_id, organization, organization_coordsys_id FROM gpkg_spatial_ref_sys"):
                if org and str(org).upper() == "EPSG" and code is not None and int(code) > 0:
                    srs_names[srs_id] = f"EPSG:{int(code)}"
        except sqlite3.DatabaseError:
            pass
        ids = {table: lid for lid, table in layer_ids([c[0] for c in contents]).items()}
        out = []
        for table, kind, identifier in contents:
            cols = [(r[1], (r[2] or "").upper()) for r in con.execute(f"PRAGMA table_info({_q(table)})")]
            names = {c for c, _ in cols}
            geom = geom_cols.get(table)
            time_field = next((f for f in TIME_FIELDS if f in names), None) or next(
                (c for c, t in cols if t in ("DATETIME", "DATE") and (not geom or c != geom[0])), None
            )
            out.append(
                {
                    "layer_id": ids[table],
                    "table": table,
                    "title": identifier or table,
                    "kind": "features" if kind == "features" else "table",
                    "geometry_column": geom[0] if geom else None,
                    "geometry_type": geom[1] if geom else None,
                    "srs_id": geom[2] if geom else None,
                    "crs": srs_names.get(geom[2]) if geom else None,
                    "fields": [c for c, _ in cols if not geom or c != geom[0]],
                    "has_ufield_uuid": "ufield_uuid" in names,
                    "time_field": time_field,
                }
            )
        return out

    # list_layers ------------------------------------------------------------

    def list_layers(self, args: dict, ctx: CallContext) -> dict:
        source = args.get("source", "all")
        query = args.get("query", "").casefold()
        layers = []
        if source in ("project", "all"):
            con = self._connect()
            try:
                for layer in self._layers(con):
                    count = con.execute(f"SELECT COUNT(*) FROM {_q(layer['table'])}").fetchone()[0]
                    layers.append(
                        {
                            "layer_id": layer["layer_id"],
                            "title": layer["title"],
                            "source": "project",
                            "table": layer["table"],
                            "kind": layer["kind"],
                            "geometry_type": layer["geometry_type"],
                            "crs": layer["crs"],
                            "feature_count": count,
                            "fields": layer["fields"],
                            "has_ufield_uuid": layer["has_ufield_uuid"],
                            "time_field": layer["time_field"],
                        }
                    )
            except sqlite3.DatabaseError as e:
                raise ToolError("project_unreadable", f"cannot read data.gpkg: {e}") from e
            finally:
                con.close()
        if source in ("registry", "all"):
            layers += self.registry_layers
        if query:
            layers = [
                l for l in layers
                if query in " ".join(str(l.get(k) or "") for k in ("layer_id", "title", "table", "provider", "category")).casefold()
            ]
        return {"layers": layers, "total": len(layers)}

    # query_features ---------------------------------------------------------

    def query_features(self, args: dict, ctx: CallContext) -> dict:
        limit = args.get("limit", 100)
        bbox = args.get("bbox")
        near = args.get("near")
        t_from, t_to = _parse_time(args.get("time_from")), _parse_time(args.get("time_to"))
        if args.get("time_from") and t_from is None or args.get("time_to") and t_to is None:
            raise ToolError("invalid_input", "time_from and time_to must be ISO 8601 date-times")
        if bbox and (bbox[0] > bbox[2] or bbox[1] > bbox[3]):
            raise ToolError("invalid_input", "bbox must be [minLon, minLat, maxLon, maxLat]")

        con = self._connect()
        try:
            layers = [l for l in self._layers(con) if l["kind"] == "features"]
            if "layer_id" in args:
                layers = [l for l in layers if l["layer_id"] == args["layer_id"]]
                if not layers:
                    raise ToolError("layer_not_found", f"no feature layer {args['layer_id']!r} in this project; see list_layers")
            if (t_from or t_to) and "layer_id" in args and not layers[0]["time_field"]:
                raise ToolError("no_time_field", f"layer {args['layer_id']!r} has no time field ({', '.join(TIME_FIELDS)} or a DATE/DATETIME column)")

            results, total, skipped = [], 0, []
            for layer in layers:
                if (t_from or t_to) and not layer["time_field"]:
                    skipped.append({"layer_id": layer["layer_id"], "reason": "no time field"})
                    continue
                where, params = [], []
                if "filter" in args:
                    try:
                        sql, p = compile_filter(args["filter"], set(layer["fields"]))
                    except FilterError as e:
                        if "layer_id" in args:
                            raise ToolError("invalid_filter", str(e)) from e
                        skipped.append({"layer_id": layer["layer_id"], "reason": str(e)})
                        continue
                    where.append(f"({sql})")
                    params += p
                tf = layer["time_field"]
                gcol = layer["geometry_column"]
                cols = [c for c in layer["fields"]]
                select = ", ".join([_q(c) for c in cols] + ([_q(gcol)] if gcol else []))
                sql = f"SELECT {select} FROM {_q(layer['table'])}" + (f" WHERE {' AND '.join(where)}" if where else "")
                srs = layer["crs"]
                if gcol and layer["srs_id"] not in (None, 4326) and srs is None:
                    skipped.append({"layer_id": layer["layer_id"], "reason": f"unknown coordinate system (srs_id {layer['srs_id']})"})
                    continue
                for row in con.execute(sql, params):
                    attrs = dict(zip(cols, row[: len(cols)]))
                    if t_from or t_to:
                        when = _parse_time(str(attrs.get(tf))) if attrs.get(tf) is not None else None
                        if when is None or (t_from and when < t_from) or (t_to and when > t_to):
                            continue
                    geom = None
                    if gcol:
                        try:
                            geom = to_wgs84(decode_gpkg_geometry(row[len(cols)]), srs)
                        except (ValueError, struct.error):
                            geom = None
                    if bbox:
                        b = _bbox_of(geom)
                        if b is None or b[2] < bbox[0] or b[0] > bbox[2] or b[3] < bbox[1] or b[1] > bbox[3]:
                            continue
                    distance = None
                    if near:
                        pts = list(_positions(geom))
                        if not pts:
                            continue
                        distance = min(haversine_m(near["lon"], near["lat"], p[0], p[1]) for p in pts)
                        if distance > near["radius_m"]:
                            continue
                    total += 1
                    if near or len(results) < limit:
                        feature = {
                            "layer_id": layer["layer_id"],
                            "ufield_uuid": attrs.get("ufield_uuid"),
                            "geometry": geom,
                            "attributes": {k: _jsonable(v) for k, v in attrs.items() if k not in ("fid", "ufield_uuid")},
                        }
                        if distance is not None:
                            feature["distance_m"] = round(distance, 1)
                        results.append(feature)
        except sqlite3.DatabaseError as e:
            raise ToolError("project_unreadable", f"cannot read data.gpkg: {e}") from e
        finally:
            con.close()
        if near:  # nearest first; the limit applies after sorting
            results = sorted(results, key=lambda f: f["distance_m"])[:limit]
        out = {"features": results, "total": total, "truncated": total > len(results), "crs": "EPSG:4326"}
        if near:
            out["distance_note"] = "distance to the nearest vertex of each geometry"
        if skipped:
            out["skipped_layers"] = skipped
        return out


def register_features(dispatcher: ToolDispatcher, project) -> None:
    features = ProjectFeatures(project)
    dispatcher.register("list_layers", features.list_layers)
    dispatcher.register("query_features", features.query_features)
