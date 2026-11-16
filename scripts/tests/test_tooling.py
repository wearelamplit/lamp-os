"""Static and stubbed checks for release workflows, the release-flash cache, build cache config and docs.
Run: python3 -m unittest discover -s scripts/tests"""
import configparser, os, re, signal, stat, subprocess, tempfile, time, unittest
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
# stub: writes a 4 MiB body of $STUB_FILL to the -o target; STUB_GET_FAIL fails after a partial write,
# STUB_GET_SLEEP stalls after one
out=""
while [ $# -gt 0 ]; do case "$1" in -o) out="$2"; shift;; esac; shift; done
echo x >> "$STUB_LOG"
[ "${STUB_GET_FAIL:-}" = 1 ] && { echo partial > "$out"; exit 18; }
[ "${STUB_GET_SLEEP:-}" = 1 ] && { echo partial > "$out"; sleep 30; }
[ "${STUB_SHORT:-}" = 1 ] && { echo short > "$out"; exit 0; }
[ "${STUB_SHORT:-}" = 2 ] && { head -c 4194303 /dev/zero > "$out"; exit 0; }
head -c 4194304 /dev/zero | tr '\\0' "$STUB_FILL" > "$out"
"""


ESPTOOL_STUB = """#!/bin/bash
# stub: records the first byte of the file written at 0x10000 into $ESP_OUT
while [ $# -gt 0 ]; do [ "$1" = 0x10000 ] && head -c1 "$2" > "$ESP_OUT"; shift; done
exit 0
"""


class FlashCache(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        d = Path(self.d.name)
        (d / "bin").mkdir()
        for n, body in (("curl", STUB_CURL), ("esptool", ESPTOOL_STUB)):
            f = d / "bin" / n
            f.write_text(body)
            f.chmod(f.stat().st_mode | stat.S_IEXEC)
        self.cache = d / "cache"
        self.log = d / "curl.log"
        self.log.write_text("")

    def tearDown(self):
        self.d.cleanup()

    def env(self, fill, tag, **extra):
        return dict(os.environ, PATH=f"{self.d.name}/bin:{os.environ['PATH']}", CACHE_DIR=str(self.cache),
                    STUB_FILL=fill, STUB_LOG=str(self.log), ESP_OUT=str(Path(self.d.name) / "flashed"), RELEASE_TAG=tag, **extra)

    def run_flash(self, fill, tag="beta", ok=True, **extra):
        r = subprocess.run(["bash", str(ROOT / "scripts/flash_signed_release.sh")], env=self.env(fill, tag, **extra),
                           capture_output=True, text=True)
        self.assertEqual(r.returncode == 0, ok, r.stderr)
        return r

    def cache_files(self):
        return sorted(p.name for p in self.cache.iterdir()) if self.cache.exists() else []

    def curl_calls(self):
        return len(self.log.read_text().split())

    def flashed(self):
        return (Path(self.d.name) / "flashed").read_bytes()

    def seed(self, tag, byte=b"O"):
        self.cache.mkdir(exist_ok=True)
        f = self.cache / f"release-standard-{tag}.bin"
        f.write_bytes(byte * 4194304)
        return f

    def test_rolling_tag_downloads_despite_cached_file(self):
        old = self.seed("beta")
        self.run_flash("N")
        self.assertEqual(self.curl_calls(), 1)
        self.assertEqual(old.read_bytes()[:1], b"O")
        self.assertEqual(self.flashed(), b"N")

    def test_rolling_tag_flashes_the_fresh_download(self):
        self.seed("beta")
        self.run_flash("N")
        self.assertEqual(self.flashed(), b"N")

    def test_rolling_tag_leaves_no_file_after_success(self):
        self.run_flash("N", tag="stable")
        self.assertEqual(self.cache_files(), [])

    def test_rolling_tag_failure_errors_and_leaves_no_file(self):
        r = self.run_flash("N", ok=False, STUB_GET_FAIL="1")
        self.assertIn("not downloaded", r.stderr)
        self.assertEqual(self.cache_files(), [])

    def test_rolling_tag_failure_never_falls_back_to_cache(self):
        self.seed("beta")
        self.run_flash("N", ok=False, STUB_GET_FAIL="1")

    def test_version_tag_downloads_once_then_reuses_cache(self):
        self.run_flash("A", tag="v1.3.0")
        self.assertEqual(self.cache_files(), ["release-standard-v1.3.0.bin"])
        self.run_flash("B", tag="v1.3.0")
        self.assertEqual(self.curl_calls(), 1)
        self.assertEqual((self.cache / "release-standard-v1.3.0.bin").read_bytes()[:1], b"A")
        self.assertEqual(self.flashed(), b"A")

    def test_short_download_is_rejected_and_never_cached(self):
        img = self.seed("v1.3.0", b"A")
        r = self.run_flash("B", tag="v1.3.0", REFRESH="1", ok=False, STUB_SHORT="1")
        self.assertIn("expected at least 4 MiB", r.stderr)
        self.assertEqual(self.cache_files(), [img.name])
        self.assertEqual(img.read_bytes()[:1], b"A")

    def test_image_one_byte_short_of_4_mib_is_rejected(self):
        self.run_flash("B", tag="v1.3.0", STUB_SHORT="2", ok=False)

    def test_short_first_download_is_not_cached(self):
        self.run_flash("B", tag="v1.3.0", STUB_SHORT="1", ok=False)
        self.assertEqual(self.cache_files(), [])

    def test_version_tag_refresh_replaces_cache(self):
        self.run_flash("A", tag="v1.3.0")
        self.run_flash("B", tag="v1.3.0", REFRESH="1")
        self.assertEqual((self.cache / "release-standard-v1.3.0.bin").read_bytes()[:1], b"B")
        self.assertEqual(len(self.cache_files()), 1)

    def test_failed_version_refresh_keeps_cache_and_errors(self):
        self.run_flash("A", tag="v1.3.0")
        r = self.run_flash("B", tag="v1.3.0", REFRESH="1", ok=False, STUB_GET_FAIL="1")
        self.assertIn("cached image was kept", r.stderr)
        self.assertEqual(self.cache_files(), ["release-standard-v1.3.0.bin"])
        self.assertEqual((self.cache / "release-standard-v1.3.0.bin").read_bytes()[:1], b"A")

    def test_failed_first_version_download_errors_and_leaves_no_file(self):
        r = self.run_flash("A", tag="v1.3.0", ok=False, STUB_GET_FAIL="1")
        self.assertIn("not downloaded", r.stderr)
        self.assertEqual(self.cache_files(), [])

    def test_local_image_makes_no_curl_call_and_touches_nothing(self):
        img = Path(self.d.name) / "local.bin"
        img.write_bytes(b"L" * 4194304)
        sibs = [Path(str(img) + e) for e in (".part", ".hdr", ".etag", ".dl")]
        for q in sibs:
            q.write_text("keep")
        before = sorted(os.listdir(self.d.name))
        for extra in ({}, {"REFRESH": "1"}):
            self.run_flash("B", IMAGE=str(img), **extra)
        self.assertEqual(img.read_bytes()[:1], b"L")
        self.assertEqual(self.curl_calls(), 0)
        self.assertFalse(self.cache.exists())
        self.assertEqual(sorted(os.listdir(self.d.name)), sorted(before + ["flashed"]))
        for q in sibs:
            self.assertEqual(q.read_text(), "keep", q.name)

    def test_interrupted_download_leaves_no_temp_file_and_keeps_cache(self):
        img = self.seed("v1.3.0", b"A")
        p = subprocess.Popen(["bash", str(ROOT / "scripts/flash_signed_release.sh")],
                             env=self.env("B", "v1.3.0", STUB_GET_SLEEP="1", REFRESH="1"),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        for _ in range(100):
            if len(self.cache_files()) > 1:
                break
            time.sleep(0.1)
        self.assertEqual(len(self.cache_files()), 2)
        os.killpg(p.pid, signal.SIGTERM)
        p.wait(timeout=10)
        self.assertEqual(self.cache_files(), [img.name])
        self.assertEqual(img.read_bytes()[:1], b"A")

    def test_interrupted_rolling_download_leaves_no_temp_file(self):
        p = subprocess.Popen(["bash", str(ROOT / "scripts/flash_signed_release.sh")],
                             env=self.env("B", "beta", STUB_GET_SLEEP="1"),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        for _ in range(100):
            if self.cache_files():
                break
            time.sleep(0.1)
        self.assertEqual(len(self.cache_files()), 1)
        os.killpg(p.pid, signal.SIGTERM)
        p.wait(timeout=10)
        self.assertEqual(self.cache_files(), [])


class BuildLock(unittest.TestCase):
    WRAP = ROOT / "scripts/with_pio_lock.sh"

    def test_concurrent_builds_never_overlap(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "log"
            job = f'echo start >> {log}; sleep 0.5; echo end >> {log}'
            env = dict(os.environ, LAMP_PIO_LOCK=str(Path(d) / "lock"))
            ps = [subprocess.Popen(["bash", str(self.WRAP), "sh", "-c", job], env=env,
                                   stderr=subprocess.PIPE, text=True) for _ in range(3)]
            errs = [p.communicate(timeout=30)[1] for p in ps]
            self.assertEqual(log.read_text().split(), ["start", "end"] * 3)
            self.assertTrue(any("waiting for another lamp firmware build" in e for e in errs))

    def test_wrapper_passes_exit_status_and_args(self):
        with tempfile.TemporaryDirectory() as d:
            env = dict(os.environ, LAMP_PIO_LOCK=str(Path(d) / "lock"))
            r = subprocess.run(["bash", str(self.WRAP), "sh", "-c", 'test "$0" = a && exit 7', "a"], env=env)
            self.assertEqual(r.returncode, 7)

    def test_every_lamp_pio_run_goes_through_the_lock(self):
        lines = []
        for name, text in [("package.json", (ROOT / "package.json").read_text())] + [
                (p.name, p.read_text()) for p in (ROOT / "scripts").glob("*.sh")]:
            lines += [(name, l) for l in text.splitlines() if re.search(r"\bpio run\b", l)]
        lamp = [(n, l) for n, l in lines if "wisp" not in l and not l.lstrip().startswith("#")]
        self.assertGreaterEqual(len(lamp), 5)
        for n, l in lamp:
            self.assertRegex(l, r"with_pio_lock\.sh\"? pio run", f"{n}: {l.strip()}")


if __name__ == "__main__":
    unittest.main()
