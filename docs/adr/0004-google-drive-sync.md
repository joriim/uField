# 0004 — Google Drive sync in the standalone app

Status: proposed, 2026-10-21

## Context

The MVP is the standalone Android app plus Google Drive sync (`CLAUDE.md`, MVP scope). Upstream Mergin Maps syncs projects only with a Mergin server (`core/merginapi.cpp`, with geodiff for GeoPackage merges). μField needs a second sync target: the user's own Google Drive.

Constraints:
- **`drive.file` scope only** (rule 8). The app sees only files it created, or files the user explicitly opened with it. It cannot browse "My Drive", so a QGIS project a user copied into Drive from their computer is invisible to it.
- No ufield-server in the MVP, so the Drive token never leaves the phone (ADR 0002 §6).
- Projects are QGIS projects: a `.qgz`, one or more GeoPackages, `media/`, `notes/`, `catalog.json` (ADR 0003 §2). GeoPackages are SQLite files and may have `-wal`/`-shm` siblings while open.

## Decision

### 1. Where projects live in Drive

The app creates one folder in the user's My Drive, **`μField/`**, and one subfolder per project. Because the app created them, `drive.file` gives it full access to everything inside.

Projects get into that folder three ways:
1. **Created on the phone**, from a template; the first sync uploads them.
2. **From a computer**, with a small desktop command (`ufield-mcp drive push <folder>`, part of `server/`) that signs in with a *desktop* OAuth client of the same Google Cloud project and uploads a QGIS project folder into `μField/`. Files created by any client of the same Cloud project count as created by the app, so the phone can sync them *(verify against the Drive API docs before building)*.
3. **Imported from a zip**: the user shares or opens a `.zip` of a QGIS project from the Drive or Files app; μField unpacks it into a new project and uploads it. This uses Android's share/open intents, not a Drive scope.

### 2. Sign-in and tokens

- Google sign-in and Drive authorisation use the **Google Identity Services AuthorizationClient** (Play services) with the `drive.file` scope, through a small Java bridge (`fi.biomitta.ufield.DriveAuth`, new file in `app/android/src/fi/biomitta/ufield/`).
- The app asks for a fresh access token before each sync and keeps it only in memory. Play services handles consent and refresh, so the app stores **no refresh token** at all.
- Requires a Google Cloud project with the OAuth consent screen (app name, logo, privacy policy, verified domain), an **Android OAuth client** registered with package `fi.biomitta.ufield` and the SHA-1 of every signing key (the CI upload key and the Play App Signing key), and a **desktop OAuth client** for the command in §1. A build signed with a throwaway CI key cannot sign in.

### 3. Sync algorithm

Per project, a local sync state file `.ufield/sync.json` records, for every synced file: its relative path, Drive file id, Drive `md5Checksum` and the local size, mtime and hash at the last sync. A file whose size and mtime are unchanged keeps its recorded hash, so large media are not re-read on every sync. The rules are in `ufield/storage/syncplan.cpp` (pure C++, unit-tested) and applied by `SyncEngine` through a `RemoteStore` interface that `GoogleDriveProvider` implements.

On sync, for each file:

| Local since last sync | Drive since last sync | Action |
|---|---|---|
| changed | unchanged | upload (update the same Drive file) |
| unchanged | changed | download |
| changed | changed | **conflict**: keep both — Drive's version under the original name, the local one as `<name> (conflict <date> <device>).<ext>`; the user resolves it |
| new | — | upload |
| — | new | download |
| deleted | unchanged | delete in Drive (moved to Drive's trash, recoverable for 30 days) |
| unchanged | deleted | delete locally |

Details:
- GeoPackages are checkpointed (WAL merged) and closed before upload. `-wal`, `-shm` and `.ufield/` are never uploaded.
- Large files (video) use Drive's resumable upload. Downloads go to a temporary file and are moved into place only when complete.
- Media originals are uploaded byte-for-byte (rule 5).
- Notes are append-only (ADR 0003), so they practically never conflict.
- Sync runs when the user asks (`sync_project`) and after saving changes when online; there is no background location or background sync service.

### 4. After the MVP

- Merge GeoPackage edits instead of making conflict copies: keep the last synced copy as a base and use **geodiff** (already an upstream dependency) to rebase local changes onto Drive's version, as upstream does for Mergin.
- Projects shared with other people, and OneDrive (same design, different API).

### 5. Code layout

- `ufield/storage/`: `StorageProvider` interface (ADR in `CLAUDE.md`), `LocalStorageProvider`, `GoogleDriveProvider` (Qt Network, Drive REST v3), the sync engine and `.ufield/sync.json`.
- `app/android/src/fi/biomitta/ufield/DriveAuth.java`: the Play services bridge. These are new files in an upstream directory; no upstream file changes.
- UI: a "Google Drive" section in the project list next to local projects, with sign-in and sync status. The QML lives in `ufield/qml/`; the hook into upstream's project screen is a marked `UFIELD:` change.
- Tools: `list_projects(storage: google_drive)`, `sync_project`, with schema tests (rule 1).

## Consequences

- No refresh token or Drive token is stored on the phone or anywhere else; signing out is only revoking access in Play services.
- Users can't point μField at an arbitrary existing Drive folder. They use the desktop command or a zip import instead. This is the cost of `drive.file`; the full `drive` scope would require Google's restricted-scope security assessment.
- Until geodiff merging (§4), editing the same GeoPackage on two devices between syncs produces a conflict copy that the user has to reconcile in QGIS.
- Drive sign-in only works with builds signed by a key registered in the Cloud project, so CI needs a stable signing key (`UFIELD_ANDROID_KEYSTORE_*` secrets) before Drive can be tested.

## Open questions

- Confirm that files created by the desktop OAuth client of the same Cloud project are visible to the Android client under `drive.file` (§1.2).
- Upstream's project screens are built around the Mergin server; how much of that UI to hide in the MVP.
