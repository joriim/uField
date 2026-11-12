# 0005 — Server hosting, per-customer servers and a shared public data hub

Status: proposed, 2026-11-12

## Context

ufield-server (after the MVP) fetches imagery and base data, runs light processing (mosaics, COGs, hillshade; Sentinel-2 runs on CDSE through openEO), proxies Claude and speech-to-text, and stores products. Later products (orthomosaic, 3DGS) add field captures and GPU work.

Customers want their server kept apart from their other systems, and from other customers. At the same time, most of the data the server fetches is public (MML, Ruokavirasto, GTK, SYKE, Copernicus, all CC BY 4.0 or equivalent) and the same tiles are fetched again and again for neighbouring farms. MML's open service is meant for small-scale use and CDSE's free quota is per account, so repeated fetches waste both.

The roadmap's phase 0 item "Server hosting in the EU (GDPR)" is still open.

## Decision

### 1. Deployment: one ufield-server per customer, plus a public data hub

- Each customer (organisation) gets its **own ufield-server instance**: a separate VM or a separate cloud project, hosted by Biomitta, or in the customer's DMZ when they want it on their own premises. Never in the customer's internal network.
- Biomitta runs one **public data hub** that holds only public data.
- Customer servers never connect to each other. They connect **outbound only** to the hub, so no inbound port is opened between customers and licence checks happen in one place.

```
 Customer A                          Biomitta                          Customer B
┌────────────────────────┐   ┌──────────────────────────┐   ┌────────────────────────┐
│ ufield-server (A)      │   │ Public data hub          │   │ ufield-server (B)      │
│  private   │  public   │──►│  STAC: public only       │◄──│  public    │  private  │
│  store     │  cache    │◄──│  intake checks           │──►│  cache     │  store    │
│            │           │   │  signed index            │   │            │           │
└───────▲────────────────┘   └──────────────────────────┘   └────────────────▲───────┘
        │ HTTPS 443 (phones, MCP)     outbound only                          │
   ✕ no route to the customer's other systems              ✕ no A ↔ B connection
```

### 2. Isolating a customer server from the customer's other systems

- **Network:** default deny in both directions.
  - Inbound: HTTPS 443 through a reverse proxy only (phones, MCP clients). Administration over WireGuard or a bastion host; no open SSH.
  - Outbound: an allowlist of the sources in `config/layers.yaml`, CDSE, the Claude API, the STT provider, the hub and the update registry. No route into the customer's internal network.
- **Identity:** not joined to the customer's AD/LDAP; no shared credentials. If the customer wants single sign-on, it is OIDC token federation, not network trust.
- **Data to the customer's systems** only through an explicit export (STAC, GeoPackage, COG) to a bucket or Drive that the customer pulls from. No network shares mounted in either direction.
- **Secrets** per instance (ADR 0002). A customer may use its own MML key and CDSE account, which also moves the per-account quotas to the customer.
- Immutable image, automatic updates, backups to a separate storage account.

### 3. Only public data crosses between servers

- **Separate stores with separate credentials.** The public cache and the private store are different buckets. The service that uploads to the hub can read only the public bucket, so a bug cannot send private data.
- **Export guard on the customer server.** An item leaves only if:
  1. its source is on the open-licence allowlist (licence in `config/layers.yaml`: CC BY 4.0, Copernicus);
  2. every input in its `ufield:provenance` chain is `public`;
  3. it is a tile on a fixed grid (MML: 2 × 2 km tiles aligned to EPSG:3067; Sentinel-2: its 10 m grid per acquisition date), so it doesn't reveal parcel boundaries;
  4. requester, triggering area, user and request time are stripped. What remains: source, tile id, acquisition time, source version, licence, attribution;
  5. uploads are batched and delayed, so timing doesn't reveal who is interested in which area.
- **Intake on the hub:**
  - each server authenticates with its own certificate (mTLS), revocable, rate-limited;
  - items are content-addressed (sha256);
  - the first copy of a tile is checked against the original source (re-fetch or spot check), so one compromised server can't spread forged imagery;
  - the hub signs its index; customer servers verify signature and hashes.
- **Reading from the hub:** a server downloads the whole index or a regional part of it, not tile by tile, because per-tile queries would reveal interest.
- **Two switches per customer:** *contribute* and *consume*. A customer who doesn't want the hub to know its areas can consume without contributing.
- **Never shared:** field captures, paid imagery (Planet, UP42), per-parcel or per-customer analyses, notes, anything `proprietary`. Derived products from proprietary inputs are released only through the release gate in ADR 0006.
- **User-published public data** (for example a farmer's drone orthophoto under CC BY) goes through an explicit publish action into its own collection, with a confirmed licence and a way to withdraw it. It isn't mixed with authority data.
- **Versioning:** the cache key includes source version or acquisition time (MML updates orthophotos, ESA reprocesses Sentinel data), so STAC provenance stays correct.

### 4. Hosting and sizing

EU hosting with data in Finland: Hetzner (Tuusula data centre) or UpCloud (Helsinki). CSC (Allas, cPouta) suits research projects, but its terms usually don't allow a commercial service; check before building on it.

Estimates, not measurements:

| Part | Size |
|---|---|
| Customer server, pilot (10–50 farms) | 4 dedicated vCPU, 16 GB RAM, 160–300 GB NVMe, object storage 0.5–1 TB |
| Customer server, one organisation, steady state | 2–4 vCPU, 8–16 GB RAM, 100–200 GB disk, own object storage for field data |
| Public data hub | small compute, object storage 1–2 TB (all of Finland's open base data), generous traffic quota |
| Products from field data (later) | GPU rented by the hour (24 GB VRAM, 30–60 min per 3DGS scene); cold storage for raw video |

Data volumes behind these numbers:

- Farm Pack: about 1 GB of raw fetches per 500 ha farm, 80–150 MB kept (`docs/layer-sources.md` §1).
- A whole region is small: South Ostrobothnia (about 13,400 km²) as 0.5 m RGB + CIR JPEG COGs is about 40–50 GB, DEM 2 m 5–10 GB, a season of Sentinel-2 NDVI about 10 GB. All of Finland's open orthophoto is under 1–2 TB. Bulk preloading goes through MML's file service or a contract, not WCS.
- Field data dominates: photos 5–10 MB, 4K video 150–400 MB/min, a drone flight 3–10 GB. In the MVP it goes to the user's Drive, not the server.

**Network:** in a data centre, bandwidth is not the bottleneck (typically 1 Gbit/s and about 20 TB/month included). The bottleneck is the phone's uplink (4G 5–20 Mbit/s: one minute of 4K video takes 2–8 minutes), so large uploads are resumable and Wi-Fi-only by default. Running a server on office or home connections is for trials only (symmetric fibre ≥ 100/100 Mbit/s, fixed IP, UPS).

## Consequences

- The architecture in `CLAUDE.md` changes from one ufield-server to per-customer servers plus the hub. Tools and the schema don't change.
- Per-instance operations: deploys, updates and backups must be automated from the start; one image, configuration per customer.
- Public data costs grow with area, not with the number of customers.
- The grid and the `ufield:visibility` fields must be fixed now: the phone's STAC catalog uses them already.
- ADR 0002's list of server secrets becomes per instance.

## Open questions

- Hosting provider and its secret manager (roadmap phase 0).
- Whether Biomitta's MML key and CDSE account serve customers that don't bring their own, and the contract terms for that.
- Aggregation and delay thresholds for anything the hub publishes, reviewed with a data protection adviser.
- MML's position on a shared cache and on regional preloading.
