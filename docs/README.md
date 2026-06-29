# docs/

This folder holds:

- [`dev/`](dev/), the **developer handbook** and reference guides (architecture,
  the mesh wire-format spec, expressions, personality, security). Start at
  [`dev/README.md`](dev/README.md).
- [`adrs/`](adrs/), the **Architecture Decision Records** — the significant,
  hard-to-reverse decisions that shape the firmware, with the alternatives they
  rejected. Start at [`adrs/README.md`](adrs/README.md).
- [`update-site/`](update-site/), the **published GitHub Pages site** for the
  browser firmware flasher (`update.lamplit.ca`): `index.html`, the ESP Web
  Tools `manifest_*.json`, `CNAME`, onboarding image. Deployed by
  [`.github/workflows/pages.yml`](../.github/workflows/pages.yml) (Pages source
  must be set to "GitHub Actions"). Don't put developer docs here.
