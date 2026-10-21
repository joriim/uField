# Upstream: Mergin Maps mobile

μField's Android app is a fork of [Mergin Maps mobile](https://github.com/MerginMaps/mobile) by Lutra Consulting.

| | |
|---|---|
| Upstream repository | https://github.com/MerginMaps/mobile |
| Imported release | `2026.4.1` (commit `f62ff3ff`) |
| Imported in | merge commit `bf7fc2db`, with full upstream history |
| Upstream licence | GPL-2.0-or-later (file headers: "version 2 of the License, or (at your option) any later version") |
| μField licence | GPL-3.0, as the "or later" clause allows. Upstream headers are kept unchanged. |

## What lives where

- `app/`, `core/`, `cmake/`, `cmake_templates/`, `vcpkg/`, `test/`, `gallery/`, `fastlane/`, `CMakeLists.txt`, `vcpkg.json`, `VCPKG_BASELINE`: upstream. Change only when unavoidable, keep the diff minimal and mark each change with `// UFIELD:` (or `# UFIELD:`) and a one-line reason (`CLAUDE.md` rule 2).
- `ufield/`, `server/`, `schema/`, `config/`, `templates/`, `docs/adr/`, `docs/roadmap.md` and the μField files in `scripts/`: μField.
- `.github/workflows/`: upstream's workflows are kept unchanged so merges stay clean, and disabled in this repository's Actions settings (they only run in `MerginMaps/mobile` anyway). μField's workflows are `ufield-android.yml`, `schema.yml` and `server.yml`.

## Changes to upstream files

Keep this list complete; it is the checklist for every upstream merge.

| File | Change | Why |
|---|---|---|
| `.gitignore` | Removed the `CLAUDE.md` entry; added Python entries | μField tracks `CLAUDE.md` files; `server/` is Python |

## Building

See `.github/workflows/ufield-android.yml`: arm64-v8a on Ubuntu, following upstream `INSTALL.md` §4.1 (Linux host), without Mergin's server API key or Trimble provider. Dependencies (Qt, QGIS, GDAL, PROJ, …) are built by vcpkg and cached as NuGet packages in this repository's GitHub Packages feed.

## Taking a new upstream release

1. `git fetch https://github.com/MerginMaps/mobile.git refs/tags/<release>:refs/remotes/upstream/<release>`
2. On a new branch from `main`: `git merge upstream/<release>`. Use a merge, not a rebase, so the history and every downstream checkout stay valid.
3. Resolve conflicts using the table above; every `UFIELD:` marker must survive.
4. Update the "Imported release" row above, and the toolchain versions in `ufield-android.yml` if upstream's `android.yml` changed them.
5. Open a PR; the Android build must be green before merging. A new `VCPKG_BASELINE` or `vcpkg.json` means a long first build while the cache refills.
