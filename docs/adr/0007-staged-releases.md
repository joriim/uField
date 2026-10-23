# 0007 — Staged releases: standalone app first, then the Biomitta service, then own servers

- Status: accepted, 2026-10-23
- Decider: Miiro / Biomitta Oy
- Relation: orders the work in ADR 0001–0006 into three releases. It doesn't change their designs. It replaces the "MVP scope" in `CLAUDE.md` with release 1 below.

## Context

By 2026-10-23 the MVP was described three different ways:

- `CLAUDE.md`: a server-heavy first release (sign-in, Farm Pack, cloud STT, voice agent, remote MCP, target areas).
- ADR 0004 (decision B): all of that, plus a self-hosted profile.
- PR #4: "a standalone Android app that works with no μField server, plus Google Drive sync".

The roadmap's phases add up to roughly 30–45 weeks for one or two developers even before decision B's additions. The Android build (Qt, QGIS, GDAL from source) is the riskiest part and gates everything else.

## Decision

Ship in three releases. Each one is useful on its own, and each builds on the previous one without rework.

### Release 1 — Standalone app (the MVP)

No μField server and no μField account.

- The rebranded Mergin fork on Google Play: package `fi.biomitta.ufield`, μField name and icons, About screen with GPL notice and a source link, Mergin cloud sign-in and sync off.
- **Storage:** `local`, `device_folder` (Android Storage Access Framework) and **Google Drive sync** with `drive.file`. Drive is connected with AppAuth on the device, and its tokens stay in the Android Keystore (`docs/licensing.md` §1.3 option B).
- **Field data:** observations, photos and video with full capture metadata (rule 5); open QGIS projects.
- **Layers:**
  - Keyless open registry layers (Ruokavirasto, GTK open licence, SYKE, Metsäkeskus, FMI), with automatic attribution.
  - MML layers only with the user's **own** MML key, stored on the device (ADR 0002 §1 "user's own service credentials").
- **Notes:** typed given notes per point (ADR 0003 §2).
- **Voice notes:** recorded offline with the audio kept; transcription arrives in release 2, and the audio stays in the project so it can be transcribed then.
- **MCP from day 0:** `ufield-mcp` (local stdio, already built) works on the synced project folder from Claude Desktop and Claude Code. It reads and writes notes, and the analysis tools are added as they are built.
- **Tools in the app:** every release-1 tool in `schema/tools.json` is implemented in ToolDispatcher and the UI (rule 1).
- **Compliance:** no accounts, so Play's account-deletion rule doesn't apply. Data safety declares data sent to the user's own Drive. Privacy policy, in-app licences screen, and a source tag per release.

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
  - **Release 1:** phases 0, 1, 2 and 4, voice-note recording from phase 6, and the release-1 parts of 7 and 8.
  - **Release 2:** phases 3 and 5, and the rest of 6.
  - **Release 3:** a new phase for the self-hosted profile.
- The release-1 app must not depend on ufield-server at all. Server-only tools return a clear "needs the μField service" error, not a crash.
