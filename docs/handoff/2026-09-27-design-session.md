# Handoff: design session 2026-09-27 (claude.ai) → repo

This PR brings four documents from a design session held outside the repo. They are **additive**; no existing file is changed. This brief lists what must be reconciled in the existing files, in order.

## Decision taken: B (hybrid)

The owner chose **B** on 2026-09-27: **one ufield-server codebase with two profiles.**

- **`service`:** Biomitta runs it for **phone-only users**. This is ADR 0002 and ADR 0003 *as written*: Google sign-in and server session, Biomitta's MML/CDSE/Anthropic/STT keys, `/agent`, cloud speech-to-text, remote MCP, Farm Pack. Project data still stays in the user's own storage.
- **`self-hosted`:** the organisation runs it on its **own wired server** (NAS + GPU over fibre) or its **own virtual server** (VPS / cloud VM). Its own users (local accounts or organisation OIDC), its own keys, its own LLM/STT, Mergin CE sync, tenants per client farm, GPU workers later. Nothing reaches Biomitta.
- Basic field collection works with **no server at all**.

So ADR 0002 and ADR 0003 are **not superseded**. They describe the `service` profile. ADR 0004 adds the `self-hosted` profile, and ADR 0006 covers the wired and virtual deployments.

## Added in this PR

| File | What it is |
|---|---|
| `docs/adr/0004-user-owned-data.md` | Decision B: the three ways to use μField, the two server profiles, what each means for storage, sign-in, keys, voice, MCP and compliance; MVP scope |
| `docs/adr/0005-p2p-public-layers.md` | Peer-to-peer sharing of openly licensed public layers (post-MVP) |
| `docs/adr/0006-on-prem-server.md` | Own server for advisory firms and modern farms: wired (NAS + GPU, fibre) and virtual (VPS / cloud VM with GPU on demand); networking, Compose profiles, tenants, GPU workers and their licence gate |
| `docs/licensing.md` | Licence and compliance analysis: code, data sources, GDPR per profile, Google/Play, user obligations, P2P, own servers |

## Facts to fix in existing files

1. **Upstream licence:** Mergin Maps mobile source headers are **GPL-2.0-or-later** (licence file GPLv2). The distributed app must be GPL-3.0, because Qt (LGPLv3) and Apache-2.0 dependencies (OpenSSL 3, zxing-cpp) can't be combined with GPLv2-only. `CLAUDE.md` rule 3 says upstream is GPL-3.0; fix it. New app files: `SPDX-License-Identifier: GPL-2.0-or-later`. `docs/licensing.md` §1.1.
2. **Google sign-in on Android:** since October 2023, new Android OAuth clients can't use custom-scheme redirects by default, and Google's recommended SDK needs proprietary Play Services (a licence risk in a GPL app).
   - Service sign-in: a Google *web* OAuth client through ufield-server with an https redirect and an App Link back to the app.
   - Drive connection: AppAuth on the device (custom scheme enabled on the native client), so Drive tokens never reach a server (keeps ADR 0002 §6).
   - `docs/licensing.md` §1.3.
3. **GTK "basic licence" layers** forbid commercial and public-service use. Only open-licence layers (CC BY 4.0 since 2025-12-01) may be in the registry; the 2023 peatland layers' licence is unverified. `docs/licensing.md` §2.
4. **Parcel privacy:** Ruokavirasto shut Peltolohkot.fi down in 2019 over parcel data, and it reopened only after location search was removed. Limit parcel queries and Farm Packs to the user's confirmed parcels; no bulk export. `docs/licensing.md` §3.1.
5. **MML's open interface isn't for high-volume services.** The `service` profile needs a plan for the MML contract service and a commercial CDSE quota as usage grows. Own servers use their own free keys.
6. **Mergin Maps CE has one workspace** (multiple workspaces are Enterprise-only), hence one CE instance per tenant on own servers (ADR 0006 §4).
7. **Play Data safety** counts any data sent off the device as "collected", even to the user's own server. `docs/licensing.md` §3.4.

## Reconciliation tasks (for Claude Code, in this order)

Follow the repo's conventions: `scripts/format_schema` and `scripts/validate_schema` after schema/registry edits; cross-field rules in `x-ufield-rules`, **never** top-level `oneOf`; test cases in `schema/tests/cases.json`; one PR per step, and stop for review after each.

1. **`CLAUDE.md`**
   - **Rule 3:** the licensing fact above.
   - **"What this is":** one codebase, two server profiles (`service` for phone-only users, `self-hosted` wired or virtual); basic collection works without a server; project data always in the user's storage or own server (ADR 0004, 0006).
   - **Architecture:** keep the current diagram as the `service` profile and add the `self-hosted` box:
     - NAS or VM storage, GPU machine or GPU-on-demand VM
     - Compose profiles `core`/`ai`/`products`/`ops`/`sharing`
     - Mergin CE per tenant
     - WireGuard mesh
     - The app talks to one server per account.
   - **New rules:**
     - **No proprietary SDKs** in the default build (CI checks the APK).
     - **Data licences and parcel privacy** (registry allow-list; automatic attribution; parcel limits).
     - **Self-hosted means nothing reaches Biomitta** (no account, telemetry or processing); **the service stores no project data** (minimisation per ADR 0004).
     - **Sharing is explicit** (P2P off by default; only `p2p_shareable` inputs).
     - **Worker and model licences** (commercially usable only; no Inria 3DGS, DUSt3R/MASt3R or NC VGGT weights; licence in STAC provenance).
     - **Tenant isolation** on own servers.
     - **Profile by configuration:** `UFIELD_PROFILE=service|self-hosted`, never a fork.
   - **MVP scope:** add the self-hosted profile (`core` + `ai` + `ops`, Mergin CE, tenants, local/OIDC users, docs for wired and virtual) and storage `device_folder`, `webdav`, `mergin_ce`. Move "MerginCE sync" from Out to In; keep OneDrive, S3, GPU workers and P2P out.
2. **ADR 0002 (credentials):**
   - Add a profile column or a section: in `self-hosted`, the MML/CDSE/LLM/STT secrets are the *organisation's*, in its own server environment (same variable names).
   - The Google sign-in → session flow applies to `service` only. Own servers authenticate local users or organisation OIDC, and are their own OAuth authorisation server for remote MCP.
   - Record the sign-in mechanics from fact 2.
   - Everything else stays.
3. **ADR 0003 (Claude, notes, voice):**
   - `/agent` in `self-hosted`: forwards to the organisation's LLM (OpenAI-compatible local server on its GPU, or its own API key, e.g. Claude).
   - Speech-to-text in `self-hosted`: Whisper on its GPU, or its own cloud STT key.
   - The voice-note queue, notes (§2) and analysis tools (§3) are unchanged in both profiles.
4. **`schema/tools.json`** (proposals; implement per repo rules):
   - `list_projects.storage`: add `device_folder`, `webdav`, `mergin_ce`.
   - `get_setup_status`: connected server and profile, tenant, GPU and workers, backups, which credentials exist (never values), sharing scope. Check overlap with `list_credentials` and merge if cleaner.
   - `connect_storage` and `connect_server`: start native UI flows; return status only; no secrets in inputs.
   - `request_product`: runs on a registered GPU worker (own servers), otherwise `unavailable`.
   - `prefetch_public_layers` and `configure_public_sharing` (ADR 0005): post-MVP, so add as planned or wait.
   - *Dropped from the session draft:* `request_farm_pack.run_on` and the on-device Farm Pack. Under B the Farm Pack always runs on the connected server.
5. **`config/layers.yaml`:**
   - `license_allowlist: [CC-BY-4.0, copernicus-open]`.
   - `p2p_shareable` per provider: ruokavirasto `false`, luke `false` until verified, others `true`.
   - `auth` comments: the same secret names are read by the Biomitta service or by the organisation's own server.
   - GTK: open-licence layers only; peatland layers and Luke MVMI disabled until licences are confirmed.
6. **`docs/layer-sources.md`:**
   - Farm Pack runs on the connected server: the Biomitta service with Biomitta's keys, or an own server with the organisation's keys.
   - CDSE scaling: commercial quota for the service; own accounts for own servers.
7. **`docs/roadmap.md`:**
   - **Phase 0:** add the MML contract-service and commercial CDSE plan for the service; the Google web OAuth client for service sign-in; the native OAuth client with custom scheme for Drive.
   - **Phase 3:** build both profiles from one codebase; the `self-hosted` extras are Mergin CE per tenant, local/OIDC users, the Compose profiles and install docs for **wired (NAS + GPU)** and **virtual (VPS + GPU on demand)**.
   - **Phase 7:** Data safety per profile (`docs/licensing.md` §3.4); account deletion for the service (already planned); data processing agreements with Anthropic, the STT provider and EU hosting; in-app licences screen and source tag per release; CI check for proprietary SDKs.
   - **After 1.0:** orthomosaic/DEM worker (NodeODM) first, then 3DGS (gsplat); a P2P sharing pilot with a farmer group; managed-server offering.
8. **Server code** (`server/src/ufield/`):
   - Introduce `UFIELD_PROFILE`.
   - `dispatcher.py`, `project.py` and `mcp_local.py` are profile-independent.
   - The remote MCP auth and `/agent` backend differ by profile.

## Still open

- Pilot group for P2P sharing (cooperative, ProAgria region, SEAMK test farms).
- Service pricing: free tier limits and a paid plan for heavy users.
- Planet partner terms, before the service offers paid imagery.
- Trademark search and registration for μField.
- A lawyer's review of `docs/licensing.md` §1.3, §3 and §4 before public release.
