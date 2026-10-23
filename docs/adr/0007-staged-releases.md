# 0007 — Staged releases: standalone app first, then the Biomitta service, then own servers

- Status: accepted, 2026-10-23
- Decider: Miiro / Biomitta Oy
- Relation: orders the work in ADR 0001–0006 into three releases. It doesn't change their designs. It replaces the "MVP scope" in `CLAUDE.md` with release 1 below.
- **Amended 2026-10-23 by ADR 0009:** release 1 has **no cloud integration** and exports to one file; Google Drive sync moves to release 1.1.

## Context

By 2026-10-23 the MVP was described three different ways:

- `CLAUDE.md`: a server-heavy first release (sign-in, Farm Pack, cloud STT, voice agent, remote MCP, target areas).
- ADR 0004 (decision B): all of that, plus a self-hosted profile.
- PR #4: "a standalone Android app that works with no μField server, plus Google Drive sync".

The roadmap's phases add up to roughly 30–45 weeks for one or two developers even before decision B's additions. The Android build (Qt, QGIS, GDAL from source) is the riskiest part and gates everything else.

## Decision

Ship in three releases. Each one is useful on its own, and each builds on the previous one without rework.

### Release 1 — Standalone field recorder (the MVP, details in ADR 0009)

No μField server, no account and no cloud integration.

- The rebranded Mergin fork on Google Play, with Mergin cloud and Mergin's tile server removed.
- A built-in project created on first launch: **points and tracks with text, voice recordings, photos and videos**. Video and voice are recorded inside the app with full capture metadata (rule 5).
- **Export to one file**, a `.ufield.zip` (GeoPackage + media + notes + STAC + QGIS project) or a GeoPackage only, through the Android share sheet or saved to a folder. Import a `.ufield.zip` the same way.
- Background map: OpenStreetMap tiles (keyless, under the OSMF policy), MML with the user's own key, or an imported offline basemap file. Recording never needs a map.
- `ufield-mcp` works on an exported project for Claude from day 0.
- **Compliance:** no accounts and no developer-held data. Privacy policy, in-app licences screen, and a source tag per release.

### Release 1.1 — Google Drive sync (ADR 0008)

`drive.file` sync of the same projects, so the phone, QGIS desktop and `ufield-mcp --drive` share one project folder.

### Release 2 — Biomitta service (`service` profile, ADR 0002/0003/0004)

- ufield-server run by Biomitta in the EU: Google sign-in via server and App Link, account deletion.
- Farm Pack, target areas, cloud speech-to-text (including release-1 voice notes), voice agent through `/agent`, remote MCP connector.
- MML contract service and commercial CDSE quota as usage grows; data processing agreements with Anthropic, the STT provider and the hosting provider.

### Release 3 — Own servers (`self-hosted` profile, ADR 0004/0006)

- Compose profiles `core`, `ai` and `ops`; Mergin CE sync per tenant; local or organisation-OIDC users.
- Install docs for wired (NAS + GPU) and virtual (VPS + GPU on demand) servers.
- First GPU worker: orthomosaic and DEM through NodeODM.

### Later

3DGS and super-resolution workers, OneDrive and S3 storage, peer-to-peer sharing of public layers (ADR 0005), paid imagery.

## Consequences

- `CLAUDE.md` "MVP scope" now points here: **MVP = release 1.**
- `schema/tools.json`: each tool gets `x-ufield-release: 1 | 2 | 3`, so codegen, tests and the app build only what the current release needs. A follow-up PR tags all 27 tools.
- Roadmap phases map onto releases:
  - **Release 1:** phases 0, 1 and 2, export/import, voice and video recording, and the release-1 parts of 7 and 8.
  - **Release 1.1:** phase 4 (Google Drive).
  - **Release 2:** phases 3 and 5, and the rest of 6.
  - **Release 3:** a new phase for the self-hosted profile.
- The release-1 app must not depend on ufield-server at all. Server-only tools return a clear "needs the μField service" error, not a crash.
