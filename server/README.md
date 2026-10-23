# ufield-server and ufield-mcp

Python side of μField (`CLAUDE.md`, ADR 0003). Today it contains:

| Module | What it does |
|---|---|
| `ufield.schema` | Loads `schema/tools.json`, exports standalone tool schemas, checks `x-ufield-rules`. Shared with `scripts/validate_schema`. |
| `ufield.dispatcher` | `ToolDispatcher`: validates every call (JSON Schema, cross-field rules, no credentials in URLs) and routes it to a handler. |
| `ufield.project` | A project folder's notes: `read_notes`, `write_note`, append-only Markdown with front matter, a STAC item per note. |
| `ufield.features` | Read-only feature tools on `data.gpkg`: `list_layers`, `query_features` (geometries as GeoJSON in EPSG:4326). |
| `ufield.mcp_local` | `ufield-mcp`, the local MCP server (stdio) for Claude Desktop and Claude Code. |

The remote server (`/agent`, authenticated layer proxy, Farm Pack jobs, remote MCP) is not built yet (roadmap phase 3).

## Develop

```sh
cd server
uv sync
uv run pytest
```

The tests include MCP round trips: a real MCP client calls `ufield-mcp` in-process.

## Use μField data from Claude

`ufield-mcp` works on one project folder: a folder with `data.gpkg`, usually synced to your computer by Google Drive for desktop. Only the tools implemented in local mode are offered; today that is `read_notes`, `write_note`, `list_layers` and `query_features`. Claude can read your notes and features and write AI notes; it cannot write in `given/`, change existing notes or change any feature.

`query_features` details:
- **Layer ids** are the GeoPackage table names; a name with characters outside `[A-Za-z0-9_-]` gets an ASCII id (`näytteet` → `naytteet`). `list_layers` shows both.
- **Geometries** come back as GeoJSON in EPSG:4326, reprojected from the layer's CRS (usually EPSG:3067).
- **`filter`** takes a safe subset of QGIS expressions, run as parameterised SQL: `=`, `!=`/`<>`, `<`, `<=`, `>`, `>=`, `AND`, `OR`, `NOT`, parentheses, `IS [NOT] NULL`, `[NOT] LIKE`, `[NOT] IN (…)`. Field names are checked against the layer. Anything else is refused with `invalid_filter`.
- **`time_from` / `time_to`** use the layer's time field: the first of `ufield_created`, `created`, `datetime`, `timestamp`, `date`, or else a DATE/DATETIME column. Layers without one are skipped and listed in `skipped_layers`.
- **`near`** distances are to the nearest vertex of each geometry, nearest first.

**Claude Code**

```sh
claude mcp add ufield -- uv run --directory /path/to/uGIS/server ufield-mcp --project "/path/to/Kotitila" --client-name claude-code
```

**Claude Desktop** (`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "ufield": {
      "command": "uv",
      "args": ["run", "--directory", "/path/to/uGIS/server", "ufield-mcp",
               "--project", "/path/to/Kotitila", "--client-name", "claude-desktop"]
    }
  }
}
```

`--client-name` is written into each AI note's front matter as `client`; without it the MCP client's own name is used. The model is not recorded, since MCP clients don't report it.

## Where notes go

| Written by | About | Folder |
|---|---|---|
| The user (app, voice transcript) | one point | `notes/<ufield_uuid>/given/` |
| The user | several points, or the project | `notes/_project/given/` |
| A model (Claude over MCP, voice agent) | one point | `notes/<ufield_uuid>/ai/` |
| A model | several points, layers, time | `analyses/` |

The author comes from which client called, never from the input. Each note also gets `catalog/items/<id>.json` (STAC, with licences from the source layers in `config/layers.yaml`), linked from `catalog.json`.
