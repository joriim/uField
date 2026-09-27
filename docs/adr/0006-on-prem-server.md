# 0006 — Own ufield-server, wired or virtual, for advisory firms and modern farms

- Status: accepted with ADR 0004 decision B, 2026-09-27
- Date: 2026-09-27
- Builds on: ADR 0004 (user-owned data), ADR 0005 (P2P public layers)

## Context

The main professional users are **consulting and advisory firms and modern farms**. They will run ufield-server (profile `self-hosted`) either **wired** (their own NAS plus a GPU machine, connected over fibre) or **virtual** (a VPS or cloud VM). That makes the own server (ADR 0004, options 2 and 3) the **primary professional deployment**; phone-only users use the Biomitta service instead. It also means the GPU products deferred from the MVP (orthomosaic, 3DGS, super-resolution) can run on the customer's own hardware without Biomitta hosting anything.

## Decision

### 1. Reference hardware: storage and compute are separate roles

| Role | What | Why |
|---|---|---|
| **Storage** (always on) | NAS: TrueNAS SCALE, Synology or QNAP, with snapshots | Projects, raw drone and photo archive, STAC assets, backups. Low power, runs 24/7. |
| **Compute** (can sleep) | Linux GPU machine running Docker, or a Proxmox VM with GPU passthrough; mounts the NAS over NFS | Farm Pack, speech-to-text, LLM, drone and 3D processing. The job runner wakes it with Wake-on-LAN and lets it sleep when idle. |
| **One-box option** | TrueNAS SCALE or Proxmox host with a GPU | Smaller farms; fewer parts, less separation. |

Sizing guidance (capabilities, not shopping lists; prices and models change):

| Tier | GPU VRAM | RAM | Usable storage | Runs comfortably |
|---|---|---|---|---|
| **Farm** | 12–16 GB | 64 GB | 8–16 TB | Whisper large-class speech-to-text, a 7–14B local LLM (quantised), Farm Packs, orthomosaics from a few hundred drone images, small 3DGS scenes |
| **Advisory firm** | 24–32 GB | 128 GB | 32 TB+ | All of the above for many clients, orthomosaics from 1,000–2,000 images, field-scale 3DGS, a ~30B local LLM (quantised) |
| **Research / large** | 48 GB+ or two GPUs | 256 GB | 64 TB+ | Parallel jobs, super-resolution training, bigger models |

- For orthomosaics (OpenDroneMap), **RAM and CPU usually limit before the GPU does**, since the GPU mainly speeds up feature extraction.
- 3DGS with gsplat fits consumer GPUs (it uses far less memory than the original implementation).
- Put **both boxes on a UPS**; rural power cuts corrupt databases.

### 2. Networking over fibre: no open ports

- **Remote access runs on a WireGuard mesh:** Tailscale, or the self-hosted Headscale control server for full self-hosting. It works behind carrier-grade NAT (CGNAT), which some Finnish fibre and mobile connections use, and needs no port forwarding.
- **Phones in the field** join the mesh with the (separate, open-source) Tailscale app and reach the server privately over mobile data. The sync policy decides what goes over mobile data: observations and photos by default. Drone image sets and video upload on farm Wi-Fi or LAN, or from the drone controller's SD card directly to the NAS.
- **Public HTTPS only when needed:** cloud AI clients (for example Claude's remote MCP connectors) call from the provider's cloud, so they need a public endpoint. Use Cloudflare Tunnel or Tailscale Funnel with OAuth in front. It's **off by default**. A local LLM on the GPU needs no public endpoint at all.
- Downloads from MML, CDSE and the other sources benefit directly from fibre.

### 3. Services: Docker Compose profiles

| Profile | Services | MVP? |
|---|---|---|
| `core` | self-hosted ufield-server (headless core, MCP, STAC catalogue, job runner), Mergin Maps CE (unmodified), PostgreSQL/PostGIS, Caddy reverse proxy | **Yes** |
| `ai` | faster-whisper (speech-to-text), an LLM server with an OpenAI-compatible API (llama.cpp, Ollama or vLLM). Can instead point at an AI server the organisation already runs (for example an Open WebUI + LiteLLM stack) | **Yes** (voice is in the MVP) |
| `products` | GPU workers: NodeODM (orthomosaic, DEM, point cloud), gsplat (3DGS), a super-resolution worker | After MVP; **orthomosaic first** |
| `sharing` | Syncthing (ADR 0005 group scope and encrypted backups) | After MVP |
| `ops` | restic backups to an offsite target, status page, update notices | Yes (backups) |
| `gnss` (optional) | NTRIP caster/RTK base software for farms with their own base station | Later |

- **Updates:** pinned versions; the app shows "update available" and the admin applies it. No silent auto-updates of major versions.
- **Backups:** NAS snapshots plus an encrypted offsite copy. Syncthing's *untrusted (encrypted) device* mode lets two farms, or a farm and its adviser, hold each other's encrypted backups without being able to read them.

### 4. Multi-client use in advisory firms (tenants)

Mergin Maps CE has **one workspace**; multiple workspaces are an Enterprise Edition feature. For a firm serving many farms:

| Option | How | Trade-off |
|---|---|---|
| **A. One CE instance per client farm (recommended)** | self-hosted ufield-server manages *tenants*; each tenant gets its own CE container, database schema, STAC catalogue and storage folder | Strong separation; easy hand-over, export and deletion per client. Slightly more containers, which are cheap. |
| B. One CE instance, per-project guest access | Clients are guests on their own projects only | Simpler; weaker separation. **Verify CE's guest-role granularity first.** |
| C. Mergin Maps EE | Commercial licence from Lutra | Native multiple workspaces; costs money. |

- Tenants appear in μField as separate server connections or projects. MCP and API tokens are **scoped to one tenant**, so an AI assistant working for client A can't see client B.
- **Farm-owned server with adviser access:** the farm invites the adviser to its mesh (limited to that one host) and to its projects. Sharing *private* projects between two independent servers (farm ↔ adviser) is a later, explicit "project share" feature, separate from ADR 0005's public-layer sharing.
- **GDPR:** an advisory firm handling a farm's data acts either as the farm's processor (needs a data processing agreement) or as a controller for its own advisory records. Tenant export (GeoPackage + STAC + media) and tenant deletion make the farm's exit clean.
- **Biomitta's two roles:** as *app publisher*, Biomitta still holds no user data (ADR 0004). As an *advisory firm using μField*, Biomitta runs its own server for its own clients like any other firm, and is controller or processor for that data under its normal consulting agreements.

### 5. GPU products become pluggable workers

- `request_product` runs if a worker for that `product_type` is registered on the user's server; otherwise it returns `unavailable` as before.
- **Worker contract:** input STAC items → output STAC items, with provenance (inputs, worker, version, **model and weights licence**, parameters).
- **Licence gate for workers and model weights.**
  - *Allowed:* MIT, BSD, Apache-2.0, GPL, and AGPL when run unmodified.
  - *Not allowed:* anything non-commercial. Examples: the original Inria 3D Gaussian Splatting code (non-commercial research licence), DUSt3R/MASt3R (CC BY-NC-SA 4.0), non-commercial VGGT weights.
  - *Commercially usable alternatives:* gsplat and nerfstudio (Apache-2.0).
  - *OpenDroneMap:* AGPL-3.0. Running it unmodified on the customer's server is fine; if Biomitta modifies it and offers it over a network, the modified source must be published.
  - *LLM weights:* prefer Apache-2.0/MIT models, and record the licence of whichever model is configured.
- The capture-fidelity rule (full EXIF, GNSS quality, orientation, video pose track) exists so that these workers get good input.

### 6. Virtual server (VPS or cloud VM)

For firms without on-site hardware, or with several sites. It's the same Compose stack and profile.

| Part | Wired (§1) | Virtual |
|---|---|---|
| Storage | NAS with snapshots | VM block storage for databases; S3-compatible object storage for the photo, drone and STAC archive; provider snapshots |
| Compute | GPU machine that sleeps | A CPU VM for `core` + `ai` (small models or an external API) + `ops`, always on. **GPU on demand:** the job runner starts a GPU VM for product jobs and stops it afterwards, so GPU hours are only paid while a job runs |
| Network | WireGuard mesh, no open ports | Public IP available, but the same default: mesh for the app and admin; only the reverse proxy (HTTPS) is public, and only if needed. Never expose Mergin CE admin or database ports |
| Backups | NAS snapshot + encrypted offsite | Provider snapshots + encrypted copy **at a different provider or on site** (restic), so one provider failure isn't a total loss |

- **Hosting location and law:** choose an EU (ideally Finnish) region. The hosting provider is the organisation's processor, so a data processing agreement with it is needed. Pick providers with a clear DPA and EU data residency.
- **Large uploads** (drone image sets) go directly from the office or farm network to object storage, not through the phone.
- **Cost profile:** a small always-on CPU VM plus pay-per-use GPU suits firms with occasional drone and 3D jobs. Continuous heavy GPU use is cheaper on the wired option.
- **Hybrid wired + virtual:** a firm can run a wired server at the office and a small virtual server as an offsite backup and remote-access point, linked with Syncthing (encrypted) or Mergin CE project sync.
- **Managed option:** Biomitta can install and maintain the VM *in the customer's own cloud account* under a support contract. The customer stays the data controller and the account owner.

## Consequences

- **MVP:** the `core`, `ai` and `ops` profiles, mesh-networking setup docs, and tenant support in self-hosted ufield-server (option A).
- **First post-MVP product:** orthomosaic and DEM from drone images through NodeODM. It's the most valuable for advisory firms and the most mature open stack.
- **Tools:** `get_setup_status` reports the tenant, GPU, registered workers and backup status; `request_product` runs on a registered worker.
- **Biomitta's business:** install and configuration service ("μField Box"), support contracts, updates and security monitoring, and consulting. The software stays GPL.

## Risks

- **Operations at farms** (updates, security, disks filling up). Mitigate with a managed support contract, a status page and alerts to the admin's phone.
- **Security of an internet-connected farm server.** Mesh network only, no open ports, per-tenant tokens, public endpoint off by default.
- **Storage growth** from drone and video archives: tiered storage and retention rules for raw images after processing.
- **Power and noise** of the GPU machine: sleep plus Wake-on-LAN; the NAS alone runs 24/7.
