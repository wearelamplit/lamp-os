"""Static and stubbed checks for release workflows, the release-flash cache, build cache config and docs.
Run: python3 -m unittest discover -s scripts/tests"""
import configparser, os, re, stat, subprocess, tempfile, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WF = ROOT / ".github/workflows"


class ReleaseWorkflows(unittest.TestCase):
    def test_every_release_workflow_ships_the_wisp_image(self):
        for name in ("release.yml", "release-main.yml", "release-beta.yml"):
            text = (WF / name).read_text()
            self.assertIn("name: wisp-firmware", text, name)
            self.assertIn("release/wisp-firmware-signed.bin", text.split("files:")[-1], name)
            self.assertRegex(text, r"mv signed/wisp/firmware-signed\.bin\s+release/wisp-firmware-signed\.bin", name)


class BuildCache(unittest.TestCase):
    def test_build_cache_dir_only_in_platformio_section(self):
        cp = configparser.RawConfigParser(strict=False)
        cp.read(ROOT / "software/lamp-os/platformio.ini")
        for sec in cp.sections():
            if sec != "platformio":
                self.assertFalse(cp.has_option(sec, "build_cache_dir"), sec)

    def test_no_global_build_cache(self):
        cp = configparser.RawConfigParser(strict=False)
        cp.read(ROOT / "software/lamp-os/platformio.ini")
        self.assertFalse(cp.has_option("platformio", "build_cache_dir"))

    def test_no_not_set_sentinels_in_custom_sdkconfig(self):
        text = (ROOT / "software/lamp-os/platformio.ini").read_text()
        self.assertNotRegex(text, r"(?m)^\s*# CONFIG_\w+ is not set")


class Docs(unittest.TestCase):
    def test_docs_do_not_deny_the_stable_tag(self):
        for p in (ROOT / "CLAUDE.md", ROOT / "docs/dev/environment/setup.md"):
            self.assertNotRegex(p.read_text(), r"no\s+[`\"]?stable[`\"]?\s+tag", p.name)


STUB_CURL = """#!/bin/bash
# stub: -I prints headers (etag from $STUB_ETAG); otherwise writes a 4 MiB body to the -o target
out=""; head=0; hdr=""
while [ $# -gt 0 ]; do case "$1" in -o) out="$2"; shift;; -D) hdr="$2"; shift;; -*I*) head=1;; esac; shift; done
echo x >> "$STUB_LOG"
if [ "$head" = 1 ]; then
  [ "${STUB_HEAD_FAIL:-}" = 1 ] && exit 22
  printf 'HTTP/2 302\\r\\nlocation: x\\r\\n\\r\\nHTTP/2 200\\r\\nETag: "%s"\\r\\n\\r\\n' "$STUB_ETAG"
  exit 0
fi
[ "${STUB_GET_FAIL:-}" = 1 ] && exit 22
[ -n "$hdr" ] && printf 'HTTP/2 200\\r\\nETag: "%s"\\r\\n\\r\\n' "${STUB_GET_ETAG:-$STUB_ETAG}" > "$hdr"
head -c 4194304 /dev/zero | tr '\\0' "$STUB_FILL" > "$out"
"""


class FlashCache(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        d = Path(self.d.name)
        (d / "bin").mkdir()
        for n, body in (("curl", STUB_CURL), ("esptool", "#!/bin/bash\nexit 0\n")):
            f = d / "bin" / n
            f.write_text(body)
            f.chmod(f.stat().st_mode | stat.S_IEXEC)
        self.cache = d / "cache"
        self.log = d / "curl.log"
        self.log.write_text("")

    def tearDown(self):
        self.d.cleanup()

    def run_flash(self, etag, fill, tag="beta", _out=False, **extra):
        env = dict(os.environ, PATH=f"{self.d.name}/bin:{os.environ['PATH']}", CACHE_DIR=str(self.cache),
                   STUB_ETAG=etag, STUB_FILL=fill, STUB_LOG=str(self.log), RELEASE_TAG=tag, **extra)
        r = subprocess.run(["bash", str(ROOT / "scripts/flash_signed_release.sh")], env=env,
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r if _out else self.cache / f"release-standard-{tag}.bin"

    def test_newer_beta_replaces_cached_image(self):
        self.run_flash("a", "A")
        img = self.run_flash("b", "B")
        self.assertEqual(img.read_bytes()[:1], b"B")

    def test_unchanged_beta_is_not_redownloaded(self):
        self.run_flash("a", "A")
        img = self.run_flash("a", "B")
        self.assertEqual(img.read_bytes()[:1], b"A")

    def test_offline_reuses_cache(self):
        self.run_flash("a", "A")
        img = self.run_flash("b", "B", STUB_HEAD_FAIL="1")
        self.assertEqual(img.read_bytes()[:1], b"A")

    def test_failed_redownload_keeps_cache(self):
        self.run_flash("a", "A")
        img = self.run_flash("b", "B", STUB_GET_FAIL="1")
        self.assertEqual(img.read_bytes()[:1], b"A")
        self.assertFalse(Path(str(img) + ".part").exists())

    def test_etag_stored_is_the_downloaded_ones(self):
        self.run_flash("new", "A", STUB_GET_ETAG="old")
        img = self.run_flash("new", "B")
        self.assertEqual(img.read_bytes()[:1], b"B")

    def test_unchecked_cache_warns(self):
        self.run_flash("a", "A")
        r = self.run_flash("b", "B", STUB_HEAD_FAIL="1", _out=True)
        self.assertIn("may be stale", r.stdout)

    def test_local_image_is_never_touched(self):
        img = Path(self.d.name) / "local.bin"
        img.write_bytes(b"L" * 4194304)
        self.run_flash("zzz", "B", IMAGE=str(img))
        self.assertEqual(img.read_bytes()[:1], b"L")
        self.assertEqual(self.log.read_text(), "")

    def test_version_tag_skips_the_check(self):
        self.run_flash("a", "A", tag="v1.3.0")
        n = len(self.log.read_text().split())
        self.run_flash("b", "B", tag="v1.3.0")
        self.assertEqual(len(self.log.read_text().split()), n)


if __name__ == "__main__":
    unittest.main()
