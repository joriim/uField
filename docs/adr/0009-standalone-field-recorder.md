# 0009 — Release 1 is a standalone field recorder: no cloud, export to one file

- Status: accepted, 2026-10-23
- Decider: Miiro / Biomitta Oy
- Relation: narrows release 1 of ADR 0007. Google Drive sync (ADR 0008) moves to **release 1.1**. Releases 2 and 3 are unchanged.

## Context

The owner wants the first release to be as independent as possible: it saves **points and tracks** with **text, voice recordings, photos and videos**, and needs nothing else. **No cloud integration in the MVP**; data leaves the phone as one exported file.

Upstream Mergin Maps mobile already has:
- an in-app project wizard (survey layer, tracking layer, background map)
- position tracking (track recording as a foreground service)
- forms, and photo capture with EXIF

It has **no** video or audio recording. Its wizard's background map comes from **Mergin's tile server** (`tiles.merginmaps.com`), and its accounts and sync go to Mergin's cloud.

## Decision

### What release 1 does

1. **Works on first launch with no setup.** No account, no sign-in, no API key. If no project exists, μField creates a "Kenttämuistio / Field notebook" project on the phone from a built-in template.
2. **Points:** tap or "here" (GNSS), with a title, free text and any number of media attachments.
3. **Tracks:** start and stop recording; the track is a line with start and end time, distance, title and text, and it can have media attachments too. This reuses upstream's position tracking.
4. **Media on points and tracks:**
   - **Photo:** upstream camera flow, EXIF kept (rule 5).
   - **Video:** recorded **inside the app** with Qt Multimedia. Because the app stays in the foreground, it logs GNSS fix and device orientation during recording into a pose track next to the video (rule 5).
   - **Voice recording:** recorded inside the app (AAC in `.m4a`). Kept as a recording; no transcription in release 1.
   - **Existing files:** picked with the Android photo picker or the Storage Access Framework, and copied unmodified.
5. **Typed notes** per point or track (ADR 0003 §2, given notes).
6. **Export to one file**, from the project screen or a tool:
   - **`<project>_<YYYY-MM-DD>.ufield.zip`** (default) contains:
     - `data.gpkg` with `points`, `tracks` and `media` (all in EPSG:4326 for interchange; the map displays in EPSG:3067)
     - `media/` with originals and pose tracks
     - `notes/`
     - `catalog.json` + `catalog/` (STAC, rule 6)
     - the QGIS project `.qgz`, so it opens directly in QGIS desktop
     - `manifest.json`: SHA-256 of every file, app version, export time, CRS, licence and attribution of any open layers used
   - **GeoPackage only** (data without media), for quick sharing.
   - The file goes out through the **Android share sheet** (e-mail, any app the user picks) or is **saved to a folder** through the Storage Access Framework (phone storage, USB stick, a cloud app's folder). μField itself talks to no cloud service; where the file goes is the user's choice.
7. **Import** a `.ufield.zip` (or a zipped QGIS project) through the Storage Access Framework, to move a project to another phone or restore a backup.
8. **Claude from day 0** without a cloud: unzip the export on the computer, or point `ufield-mcp` at it, and Claude reads points, tracks, media metadata and notes and writes AI notes. Reading the `.ufield.zip` directly is a small follow-up to `ufield-mcp`.

### Data model (template project)

| Layer / table | Geometry | Key fields |
|---|---|---|
| `points` | Point | `ufield_uuid`, `ufield_created` (UTC ISO 8601), `title`, `text`, GNSS fix fields (accuracy, fix type incl. RTK, satellites) |
| `tracks` | LineString | `ufield_uuid`, `ufield_created`, `title`, `text`, `tracking_start_time`, `tracking_end_time`, `total_distance` (upstream fields) |
| `media` (attributes table) | — | `ufield_uuid`, `feature_uuid` (the point or track), `kind` (photo, video, audio), `path`, `sha256`, `mime_type`, `captured_at`, `duration_s`, GNSS fix and orientation at capture, `pose_track_path` (video) |

- `media` is related to `points` and `tracks` through `feature_uuid`. The form shows it as a gallery with photo, video and voice buttons.
- Features are always addressed by `ufield_uuid` (`CLAUDE.md`).

### Background map without Mergin's servers

- **Default online map:** OpenStreetMap standard tiles (`tile.openstreetmap.org`, keyless), under the OSMF tile usage policy:
  - a distinctive, stable User-Agent (`μField/<version> (fi.biomitta.ufield; <contact>)`)
  - OSM attribution always visible on the map
  - cache headers honoured
  - **no offline or pre-download use**
- **Optional:** MML background map with the user's own free API key (Keystore, ADR 0002).
- **Offline maps:** the user imports a basemap file (GeoPackage or MBTiles) through the Storage Access Framework.
- **Points and tracks never depend on a map:** recording works offline and with no basemap at all.

### Removed from release 1

- Google Drive sync and Google sign-in (ADR 0008 → release 1.1)
- Mergin cloud sign-in, project lists and sync UI (hidden in the μField build; the code stays upstream)
- `tiles.merginmaps.com`, and help, support and changelog links to Mergin, replaced or hidden
- Any μField server feature (release 2)

The app's only network traffic in release 1 is map tiles from the services the user enables.

## Consequences

- **`schema/tools.json`** (follow-up PR):
  - new release-1 tools `create_project`, `start_track`, `stop_track`, `export_project` and `import_project`
  - `sync_project` moves to release 1.1
  - `attach_media` already covers photo, video and audio
  - `add_voice_note` records only in release 1 (transcription in release 2)
- **Permissions:** `RECORD_AUDIO` is added; `INTERNET` stays for map tiles. The Data safety form gets simpler: data leaves the phone only through user-initiated export or share (`docs/play-policy.md`).
- **Code:**
  - `ufield/` gets the template project, in-app video and audio recording with pose logging, the media table and gallery, export/import (QGIS `QgsZipUtils` for the ZIP) and the OSM User-Agent.
  - Upstream files change only where the wizard's tile URL and the Mergin cloud UI are switched off (`// UFIELD:`).
- **Release 1.1:** Google Drive sync (ADR 0008).
- **Release 2:** transcription of voice recordings, Farm Pack, voice agent and the other service features, as planned.
- **Tests:** export, then import, round trip with byte-identical media and matching SHA-256; a QGIS-desktop open check of the exported `.qgz` in CI (headless QGIS); the video pose track has one record per sampled frame time.

## Open questions

- The contact address in the OSM User-Agent, and Biomitta's support e-mail for the About screen.
- The size limit for share-sheet exports. Large video projects may need "save to folder" rather than e-mail.
