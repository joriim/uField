# Dependencies

Every third-party dependency, its licence and where it is used. Rule 3 in `CLAUDE.md`: licences must be compatible with GPL-3.0; check before adding.

## Development and CI tools (not shipped in the app or server)

| Package | Licence | Used by |
|---|---|---|
| jsonschema (with its dependency referencing) | MIT | `scripts/validate_schema` |
| PyYAML | MIT | `scripts/validate_schema` |

## App

Beyond upstream Mergin Maps mobile (see upstream for its list):

| Package | Licence | Used for |
|---|---|---|
| `com.google.android.gms:play-services-auth` 21.3.0 | Android Software Development Kit License (proprietary, distributed by Google) | Google sign-in and Drive authorisation for Drive sync (ADR 0004) |

Google Play services is not GPL-compatible source code; like upstream's `play-services-location`, it is a platform library supplied through Google Play services on the device, which μField links against as upstream already does. The μField and Mergin code stays GPL. A build for devices without Play services (e.g. F-Droid) would have to leave out Drive sync and location from Play services; record that decision in an ADR if it comes up.

Qt Network and Qt Core (`ufield/storage`) are already upstream dependencies. Planned: qtkeychain (BSD-3-Clause, ADR 0002).

## Server (`server/`, direct dependencies)

| Package | Licence | Used for |
|---|---|---|
| mcp (with mcp-types) | MIT | MCP server (`ufield-mcp`) |
| jsonschema | MIT | tool input validation in `ToolDispatcher` |
| PyYAML | MIT | layer registry, note front matter |
| hatchling (build only) | MIT | building the wheel |
| pytest (dev only) | MIT | tests |

Main transitive dependencies of mcp: anyio, pydantic (MIT), starlette (BSD-3-Clause). The full pinned set is in `server/uv.lock`.
