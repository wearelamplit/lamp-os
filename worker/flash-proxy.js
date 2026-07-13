// Streams lamp firmware images from GitHub Releases so the web installer can
// fetch them same-origin. GitHub's asset host sends no Access-Control-Allow-Origin,
// so ESP Web Tools cannot fetch a release URL directly; this Worker sits on
// update.lamplit.ca/dl/* (same origin as the flasher page) and proxies through.
//
// Route (wrangler.toml): update.lamplit.ca/dl/*
//   /dl/stable/<asset> -> releases/latest/download/<asset>  (newest non-prerelease tag)
//   /dl/beta/<asset>   -> releases/download/beta/<asset>    (rolling prerelease tag)
// Both are built-in GitHub redirects, so no API call and no rate limit.

const REPO = "wearelamplit/lamp-os";
const UPSTREAM = {
  stable: (asset) => `https://github.com/${REPO}/releases/latest/download/${asset}`,
  beta: (asset) => `https://github.com/${REPO}/releases/download/beta/${asset}`,
};

export default {
  async fetch(req) {
    const { pathname } = new URL(req.url);
    const m = pathname.match(/^\/dl\/(stable|beta)\/([\w.-]+)$/);
    if (!m) return new Response("not found", { status: 404 });
    const [, channel, asset] = m;

    const upstream = await fetch(UPSTREAM[channel](asset), { redirect: "follow" });
    return new Response(upstream.body, {
      status: upstream.status,
      headers: {
        "content-type": "application/octet-stream",
        "access-control-allow-origin": "*",
        "cache-control": "public, max-age=300",
      },
    });
  },
};
