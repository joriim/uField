# SPDX-License-Identifier: GPL-3.0-or-later
"""list_layers and query_features on a GeoPackage with real geometries."""

import sqlite3
import struct

import pytest

from ufield.dispatcher import CallContext, Client, ToolDispatcher, ToolError
from ufield.features import compile_filter, decode_gpkg_geometry, FilterError, haversine_m, layer_ids, register_features
from ufield.project import Project

MCP = CallContext(Client.MCP, client_name="test")
P1 = "11111111-1111-4111-8111-111111111111"
P2 = "22222222-2222-4222-8222-222222222222"
P3 = "33333333-3333-4333-8333-333333333333"
L1 = "44444444-4444-4444-8444-444444444444"

REGISTRY_LAYERS = [
    {"layer_id": "gtk_maapera", "title": "Maaperä", "source": "registry", "provider": "GTK", "category": "soil",
     "service_type": "wms", "license": "CC-BY-4.0", "attribution": "© GTK", "needs_credentials": False},
]


def gpkg_point(x, y, srs_id, big_endian=False):
    order = ">" if big_endian else "<"
    flags = 0 if big_endian else 1  # bit 0: header byte order; envelope 0
    header = b"GP" + bytes([0, flags]) + struct.pack(order + "i", srs_id)
    return header + bytes([0 if big_endian else 1]) + struct.pack(order + "Idd", 1, x, y)


def gpkg_line(points, srs_id):
    header = b"GP" + bytes([0, 1]) + struct.pack("<i", srs_id)
    body = bytes([1]) + struct.pack("<II", 2, len(points)) + b"".join(struct.pack("<dd", *p) for p in points)
    return header + body


def make_project(tmp_path):
    con = sqlite3.connect(tmp_path / "data.gpkg")
    con.executescript(
        """
        CREATE TABLE gpkg_spatial_ref_sys (srs_name TEXT, srs_id INTEGER PRIMARY KEY, organization TEXT,
            organization_coordsys_id INTEGER, definition TEXT);
        INSERT INTO gpkg_spatial_ref_sys VALUES ('WGS 84', 4326, 'EPSG', 4326, ''), ('ETRS89 / TM35FIN', 3067, 'EPSG', 3067, '');
        CREATE TABLE gpkg_contents (table_name TEXT PRIMARY KEY, data_type TEXT, identifier TEXT);
        INSERT INTO gpkg_contents VALUES ('näytteet', 'features', 'Maanäytteet'), ('ojat', 'features', 'Ojat'),
            ('lomake', 'attributes', 'Lomake');
        CREATE TABLE gpkg_geometry_columns (table_name TEXT, column_name TEXT, geometry_type_name TEXT, srs_id INTEGER);
        INSERT INTO gpkg_geometry_columns VALUES ('näytteet', 'geom', 'POINT', 3067), ('ojat', 'geom', 'LINESTRING', 4326);
        CREATE TABLE "näytteet" (fid INTEGER PRIMARY KEY, ufield_uuid TEXT, ph REAL, nimi TEXT, ufield_created TEXT, geom BLOB);
        CREATE TABLE ojat (fid INTEGER PRIMARY KEY, ufield_uuid TEXT, kunto TEXT, geom BLOB);
        CREATE TABLE lomake (fid INTEGER PRIMARY KEY, teksti TEXT);
        """
    )
    # Three samples in TM35FIN: 62.2318 N 27.0 E, 1.1 km north of it, and 20 km east.
    con.executemany(
        'INSERT INTO "näytteet" (ufield_uuid, ph, nimi, ufield_created, geom) VALUES (?, ?, ?, ?, ?)',
        [
            (P1, 5.8, "Alavainio", "2026-05-02T08:00:00Z", gpkg_point(500000, 6900000, 3067)),
            (P2, 6.4, "Mäkipelto", "2026-06-15T12:30:00+03:00", gpkg_point(500000, 6901100, 3067, big_endian=True)),
            (P3, None, "Rantapelto", None, gpkg_point(520000, 6900000, 3067)),
        ],
    )
    con.execute("INSERT INTO ojat (ufield_uuid, kunto, geom) VALUES (?, 'hyvä', ?)", (L1, gpkg_line([(26.99, 62.24), (27.01, 62.26)], 4326)))
    con.commit()
    con.close()
    return Project(tmp_path, registry={})


@pytest.fixture
def d(tmp_path):
    dispatcher = ToolDispatcher()
    register_features(dispatcher, make_project(tmp_path))
    dispatcher._handlers["list_layers"].__self__.registry_layers = REGISTRY_LAYERS
    return dispatcher


# -- helpers ---------------------------------------------------------------------


def test_layer_ids_are_ascii_and_unique():
    ids = layer_ids(["näytteet", "naytteet", "ojat", "Pellon reunat"])
    assert ids == {"naytteet": "näytteet", "naytteet_2": "naytteet", "ojat": "ojat", "Pellon_reunat": "Pellon reunat"}


def test_decodes_points_in_both_byte_orders_and_lines():
    assert decode_gpkg_geometry(gpkg_point(1.5, 2.5, 4326)) == {"type": "Point", "coordinates": [1.5, 2.5]}
    assert decode_gpkg_geometry(gpkg_point(1.5, 2.5, 4326, big_endian=True)) == {"type": "Point", "coordinates": [1.5, 2.5]}
    line = decode_gpkg_geometry(gpkg_line([(0, 0), (1, 1)], 4326))
    assert line == {"type": "LineString", "coordinates": [[0, 0], [1, 1]]}
    assert decode_gpkg_geometry(None) is None
    with pytest.raises(ValueError):
        decode_gpkg_geometry(b"not a geometry")


def test_filter_compiles_to_parameterised_sql():
    cols = {"ph", "nimi", "kunto"}
    sql, params = compile_filter("ph < 6 AND (nimi LIKE 'A%' OR \"kunto\" IS NOT NULL)", cols)
    assert sql == '"ph" < ? AND ("nimi" LIKE ? OR "kunto" IS NOT NULL)'
    assert params == [6, "A%"]
    sql, params = compile_filter("nimi NOT IN ('a', 'b''c') AND NOT ph >= 7.5", cols)
    assert sql == '"nimi" NOT IN (?, ?) AND NOT ("ph" >= ?)'
    assert params == ["a", "b'c", 7.5]


@pytest.mark.parametrize(
    "expr",
    ["ph < 6; DROP TABLE x", "missing = 1", "ph <", "ph < 6 AND", "(ph < 6", "ph = 6)", "nimi NOT = 'a'"],
)
def test_filter_rejects_anything_outside_the_subset(expr):
    with pytest.raises(FilterError):
        compile_filter(expr, {"ph", "nimi"})


def test_haversine():
    assert haversine_m(27.0, 62.0, 27.0, 62.01) == pytest.approx(1112, abs=2)


# -- list_layers -------------------------------------------------------------------


def test_list_layers_project_and_registry(d):
    out = d.call("list_layers", {}, MCP)
    by_id = {l["layer_id"]: l for l in out["layers"]}
    assert set(by_id) == {"naytteet", "ojat", "lomake", "gtk_maapera"}
    samples = by_id["naytteet"]
    assert samples["table"] == "näytteet" and samples["title"] == "Maanäytteet"
    assert samples["feature_count"] == 3 and samples["crs"] == "EPSG:3067"
    assert samples["time_field"] == "ufield_created" and samples["has_ufield_uuid"] is True
    assert "geom" not in samples["fields"]
    assert by_id["lomake"]["kind"] == "table"
    assert by_id["gtk_maapera"]["license"] == "CC-BY-4.0"


def test_list_layers_source_and_query(d):
    assert {l["layer_id"] for l in d.call("list_layers", {"source": "project"}, MCP)["layers"]} == {"naytteet", "ojat", "lomake"}
    assert [l["layer_id"] for l in d.call("list_layers", {"source": "registry"}, MCP)["layers"]] == ["gtk_maapera"]
    assert [l["layer_id"] for l in d.call("list_layers", {"query": "MAANÄYT"}, MCP)["layers"]] == ["naytteet"]


# -- query_features ----------------------------------------------------------------


def test_all_features_reprojected_to_wgs84(d):
    out = d.call("query_features", {}, MCP)
    assert out["total"] == 4 and out["crs"] == "EPSG:4326" and not out["truncated"]
    by_uuid = {f["ufield_uuid"]: f for f in out["features"]}
    lon, lat = by_uuid[P1]["geometry"]["coordinates"]
    assert lon == pytest.approx(27.0, abs=1e-6) and lat == pytest.approx(62.2318, abs=1e-3)
    assert by_uuid[P1]["attributes"] == {"ph": 5.8, "nimi": "Alavainio", "ufield_created": "2026-05-02T08:00:00Z"}
    assert by_uuid[L1]["geometry"]["type"] == "LineString"


def test_filter_and_layer(d):
    out = d.call("query_features", {"layer_id": "naytteet", "filter": "ph < 6"}, MCP)
    assert [f["ufield_uuid"] for f in out["features"]] == [P1]
    with pytest.raises(ToolError) as e:
        d.call("query_features", {"layer_id": "naytteet", "filter": "kunto = 'hyvä'"}, MCP)
    assert e.value.code == "invalid_filter"
    # Without a layer, layers the filter doesn't fit are skipped, not errors.
    out = d.call("query_features", {"filter": "kunto = 'hyvä'"}, MCP)
    assert [f["ufield_uuid"] for f in out["features"]] == [L1]
    assert out["skipped_layers"][0]["layer_id"] == "naytteet"


def test_unknown_layer(d):
    with pytest.raises(ToolError) as e:
        d.call("query_features", {"layer_id": "nope"}, MCP)
    assert e.value.code == "layer_not_found"


def test_near_sorts_by_distance_and_limits_after_sorting(d):
    out = d.call("query_features", {"layer_id": "naytteet", "near": {"lat": 62.23177, "lon": 27.0, "radius_m": 1500}, "limit": 1}, MCP)
    assert out["total"] == 2 and out["truncated"]
    assert [f["ufield_uuid"] for f in out["features"]] == [P1]
    assert out["features"][0]["distance_m"] < 5


def test_bbox(d):
    out = d.call("query_features", {"bbox": [26.95, 62.2, 27.05, 62.24]}, MCP)
    assert {f["ufield_uuid"] for f in out["features"]} == {P1, L1}


def test_time_range_uses_the_time_field(d):
    out = d.call("query_features", {"time_from": "2026-06-01T00:00:00Z"}, MCP)
    assert [f["ufield_uuid"] for f in out["features"]] == [P2]
    assert {s["layer_id"] for s in out["skipped_layers"]} == {"ojat"}
    out = d.call("query_features", {"layer_id": "naytteet", "time_to": "2026-05-31T23:59:59Z"}, MCP)
    assert [f["ufield_uuid"] for f in out["features"]] == [P1]
    with pytest.raises(ToolError) as e:
        d.call("query_features", {"layer_id": "ojat", "time_from": "2026-01-01T00:00:00Z"}, MCP)
    assert e.value.code == "no_time_field"


def test_read_only(d, tmp_path):
    before = (tmp_path / "data.gpkg").read_bytes()
    d.call("query_features", {"filter": "ph < 6"}, MCP)
    d.call("list_layers", {}, MCP)
    assert (tmp_path / "data.gpkg").read_bytes() == before
