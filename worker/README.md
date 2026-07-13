# flash-proxy

Cloudflare Worker that lets the web installer at update.lamplit.ca pull firmware
straight from GitHub Releases. GitHub's release-asset host sends no CORS header,
so ESP Web Tools can't fetch a release URL directly; this Worker proxies it on a
same-origin path.

- `/dl/stable/<asset>` → newest non-prerelease release (`releases/latest`)
- `/dl/beta/<asset>` → rolling `beta` prerelease

Both channels self-update: cut a new release, the site serves it — no committed
binaries, no manifest edits.

## Deploy

```sh
cd worker
npx wrangler deploy        # first run prompts a Cloudflare login
```

The route (`update.lamplit.ca/dl/*`) is declared in `wrangler.toml`. Verify:

```sh
curl -sI https://update.lamplit.ca/dl/stable/distribution-standard.bin | head -1
curl -sI https://update.lamplit.ca/dl/beta/distribution-standard.bin   | head -1
```
