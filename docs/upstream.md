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
- `ufield/`, `app/android/src/fi/biomitta/ufield/` (new Java files in an upstream folder), `server/`, `schema/`, `config/`, `templates/`, `docs/adr/`, `docs/roadmap.md` and the μField files in `scripts/`: μField.
- `.github/workflows/`: upstream's workflows are kept unchanged so merges stay clean, and disabled in this repository's Actions settings (they only run in `MerginMaps/mobile` anyway). μField's workflows are `ufield-android.yml`, `schema.yml` and `server.yml`.

## Changes to upstream files

Keep this list complete; it is the checklist for every upstream merge.

| File | Change | Why |
|---|---|---|
| `.gitignore` | Removed the `CLAUDE.md` entry; added Python entries | μField tracks `CLAUDE.md` files; `server/` is Python |
| `cmake_templates/build.gradle.in` | `applicationId 'fi.biomitta.ufield'` | μField's identity on Play and devices. The Java namespace and classes stay `uk.co.lutraconsulting`, so upstream Java and JNI code is unchanged |
| `cmake_templates/AndroidManifest.xml.in` | Labels `μField`; file provider authority `${applicationId}.fileprovider`; removed the `hello.merginmaps.com` SSO deep link | Rebrand; authorities must be unique per device so μField can be installed next to Mergin Maps; μField must not claim Mergin's web links |
| `app/android/src/uk/co/lutraconsulting/CameraActivity.java`, `MMActivity.java` | File provider authority from `getPackageName()` | Follows the applicationId |
| `app/androidutils.cpp` | Camera intent's package from `getPackageName()` | `setClassName` needs the applicationId, which is no longer `uk.co.lutraconsulting` |
| `app/android/res/xml/file_paths.xml` | `Android/data/fi.biomitta.ufield/` | The app's external directory follows the applicationId |
| `app/qml/main.qml` | Window title `μField` | Rebrand |
| `CMakeLists.txt` | `add_subdirectory` of `ufield/storage`, `ufield/i18n`, `ufield/tools` | Builds μField's libraries with the app |
| `app/CMakeLists.txt` | Adds `ufield/app/ufieldapp.cpp` to the app; links `ufield_storagetools` and `ufield_i18n` | μField tools, Drive sync (ADR 0004), translations |
| `app/main.cpp` | Includes `ufieldapp.h` and `ufieldtranslations.h`; creates `ufield::UfieldApp` (ToolDispatcher as `__ufieldTools`, Drive sync as `__ufieldDriveSync`); installs μField translations after upstream's | μField tools and Drive sync; μField strings in Finnish |
| `app/CMakeLists.txt` (QML) | Adds `ufield/qml` files to the QML module under upstream paths (`project/MMDriveProjectsTab.qml`) | μField QML lives in `ufield/qml` |
| `app/qml/project/MMProjectController.qml` | Fourth footer tab "Drive" (`drive` state) showing `MMDriveProjectsTab`; footer buttons are a quarter wide | Google Drive projects in the project list |
| `cmake_templates/build.gradle.in` | `play-services-auth` dependency | Google sign-in and Drive authorisation (AuthorizationClient) |
| `cmake_templates/AndroidManifest.xml.in` | `fi.biomitta.ufield.DriveAuthActivity` | Shows Google's consent screen without changing `MMActivity` |

Rebrand still to do: the launcher icon, splash screen and colours (need μField artwork); user-visible "Mergin Maps" strings in `tr()` texts, together with their Finnish and English translations; turning off Mergin cloud sign-in by default.

## Building

See `.github/workflows/ufield-android.yml`: arm64-v8a on Ubuntu, following upstream `INSTALL.md` §4.1 (Linux host), without Mergin's server API key or Trimble provider. Dependencies (Qt, QGIS, GDAL, PROJ, …) are built by vcpkg and cached as NuGet packages in this repository's GitHub Packages feed.

## Taking a new upstream release

1. `git fetch https://github.com/MerginMaps/mobile.git refs/tags/<release>:refs/remotes/upstream/<release>`
2. On a new branch from `main`: `git merge upstream/<release>`. Use a merge, not a rebase, so the history and every downstream checkout stay valid.
3. Resolve conflicts using the table above; every `UFIELD:` marker must survive.
4. Update the "Imported release" row above, and the toolchain versions in `ufield-android.yml` if upstream's `android.yml` changed them.
5. Open a PR; the Android build must be green before merging. A new `VCPKG_BASELINE` or `vcpkg.json` means a long first build while the cache refills.
