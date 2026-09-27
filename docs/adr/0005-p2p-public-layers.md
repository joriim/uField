# 0005 — Sharing public data layers peer to peer

- Status: proposed (after MVP)
- Date: 2026-09-27
- Depends on: ADR 0004 (user-owned data)

## Question

Can μField users share the public layers they've downloaded or computed (orthophotos, elevation, Sentinel-2 composites, open vector layers) directly with each other instead of each fetching them from the source?

## Short answer

**Yes, for openly licensed layers, with conditions.**

- **Licences:** CC BY 4.0 and the Copernicus licence allow redistribution, provided the attribution, licence and "modified" notice travel with the data.
- **Where the value is:** less in raw downloads (a farm's orthophoto is 15–40 MB, cheap to fetch directly) and more in three cases:
  1. **Computed products:** a Sentinel-2 season composite for a municipality costs openEO credits once and can then be reused by every farm in that area.
  2. **Offline and poor-coverage areas:** sharing over the farm's local network, and between neighbouring farms.
  3. **Resilience:** less dependence on free interfaces with usage limits.
- **Two risks to design around:**
  1. **Location leakage:** asking peers for "the tile around my farm" tells them where the farm is.
  2. **Data that must not travel:** paid imagery, GTK basic-licence layers, parcel attributes and the user's own observations.

## What may be shared

| Data | Share P2P? | Reason |
|---|---|---|
| MML orthophotos, DEM, background maps (CC BY 4.0) | **Yes** | Redistribution allowed with attribution; reduces load on MML's small-scale open interface. |
| Sentinel-2 raw scenes and derived composites and indices | **Yes** | Copernicus licence allows redistribution; mark derived ones "Contains modified Copernicus Sentinel data [year]". Sharing *results* is not the account multiplication CDSE forbids. |
| GTK open-licence soil maps, SYKE, Metsäkeskus (CC BY 4.0) | **Yes** | Same as MML. |
| FMI weather summaries (CC BY 4.0) | Yes, but low value | Small; fetch directly. |
| **Ruokavirasto parcel data** | **No** (each client fetches directly) | CC BY allows it, but bulk redistribution of crop and organic status per parcel is exactly the Peltolohkot.fi problem (docs/licensing.md §3.1). The files are small anyway. |
| GTK basic-licence layers | **Never** | Licence forbids transfer and public services. |
| Planet, UP42 and other paid imagery | **Never** | Licensee-only EULAs. |
| The user's own observations, photos, Farm Pack private parts | **Never** automatically | That's sharing the user's own data, a separate, explicit feature. |

The enforcement is the licence allow-list in `config/layers.yaml` plus a new `p2p_shareable: true|false` flag per layer. An asset is shareable only if **every** input in its STAC provenance is shareable.

## Design

### Unit of sharing: region packages, not tiles

- Share **fixed grid packages** instead of individual tiles: MML TM35 map sheets at the 1:50,000 sheet size (about 24 × 12 km), per source and per source date. Each package is a STAC collection plus COGs or GeoPackages, with `LICENSES.md` and attribution inside.
- **Content addressing:** each package is identified by the hash of its manifest. Clients verify every file hash on download, so a peer can't inject a fake orthophoto.
- **Provenance and trust:** the peer that built a package from the source signs the manifest. Clients accept packages signed by keys on their trust list: their own server, their group's server, and optionally a Biomitta "reference builder" key. The last one is a signing key, not a data host. Anything unverifiable falls back to the original source.
- **Freshness:** the manifest records the source date. The signed registry (ADR 0004) says which source versions are current, so stale packages are ignored.

### Location privacy

- Coarse regions: a 24 × 12 km sheet reveals roughly the municipality, not the farm.
- **Cover traffic:** when prefetching, also request the surrounding sheets (for example the 3 × 3 block).
- **Prefer trusted groups over the public internet.** Sharing is off by default; the user picks one scope:
  - `off`
  - `lan`: same Wi-Fi or local network, for example phone ↔ farm server ↔ farmhand's phone
  - `group`: named peers, such as a cooperative, a ProAgria region or a research site
  - `public`: open swarm; opt-in, shows a clear warning that peers see your IP address and region
- Under GDPR an IP address is personal data. In `group` and `public` modes, peers see each other's IP addresses; the privacy policy and the opt-in screen must say so.

### Transport: staged, pluggable

| Stage | Transport | Why | Licence |
|---|---|---|---|
| 1 | **LAN:** mDNS discovery + HTTPS between μField apps and servers | Covers offline farms and fieldwork, simple, no NAT problems | Qt networking |
| 1 | **Federation between self-hosted ufield-servers:** each server can publish a read-only STAC catalogue of its shareable packages; group members list each other's URLs | Reuses the STAC catalogue we already need; works through Tailscale or Cloudflare Tunnel | own code |
| 2 | **Syncthing** for `group` scope: a shared "public-layers" folder between group servers | Mature, encrypted, trusted-device model, no global content index, no location leak outside the group | MPL-2.0 (GPL-compatible) |
| 3 (if demand) | **BitTorrent v2** (libtorrent) with the source services as HTTP web seeds, for `public` scope and large national packages | Best for many downloaders of the same big files | BSD-3-Clause |
| Evaluate | iroh (Rust, content-addressed blobs, NAT traversal) | Modern alternative to stages 2–3; run your own relay servers rather than the project's default public ones | MIT / Apache-2.0 |

IPFS is not recommended: its public content lookup broadcasts what you're looking for, which leaks location, and it's heavy on phones.

### Phones vs servers

- **Phones mostly download**, seeding only in `lan` mode while charging on Wi-Fi. Android background limits, battery life and mobile data costs make phones poor seeders.
- **self-hosted ufield-servers** (always on, on NAS or PC) are the seeders and group hubs.
- On Android, P2P transfers run as a foreground service of type `dataSync` with a visible notification. Play requires this to be declared and justified.

## Tools (tool parity)

- `prefetch_public_layers`: fetch shareable packages for an area (with cover sheets) from peers first, then from the source, for offline use.
- `configure_public_sharing`: set the scope (`off | lan | group | public`) and group peers. Trust keys and peer addresses can be viewed here, but secrets are only entered in the UI.
- `get_setup_status` also reports the sharing scope, peer count and cache size.

## Licence and compliance checklist

- Every package carries attribution, a licence link and a "modified" notice (CC BY 4.0 §3). No DRM on shared packages.
- The share filter is derived from the licence allow-list plus `p2p_shareable` plus provenance. CI tests that paid, basic-licence and parcel layers can never enter a package.
- Privacy policy and Play Data safety: in `group`/`public` scope, the IP address and requested region are **shared with other users** (declare it).
- Biomitta runs no tracker or index, in line with ADR 0004. The optional reference-builder signing key holds no user data.
- Sharing stays **off by default**.

## Open questions

- Is there a farmer group (cooperative, ProAgria region, SEAMK test farms) to pilot `group` scope with Syncthing?
- Which products are worth precomputing per region: season NDVI composites, ortho mosaics per municipality?
- Package size and grid: 1:50,000 sheets, or larger for less location detail?
