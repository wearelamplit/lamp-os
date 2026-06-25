// Pull signed firmware artifacts from GitHub Releases.
//
// The CI workflow (.github/workflows/release.yml + release-beta.yml)
// publishes `lamp-firmware-signed.bin` as a release asset on every
// tagged push. We fetch that asset over HTTPS, hand it to the verifier
// for LSIG + Ed25519 check, and return the bytes for the OTA pusher.
//
// No GitHub auth token needed — the releases are public. No API rate
// limit risk for the per-user fetch cadence (one tap per user per OTA
// session).

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

class FirmwareDownloadException implements Exception {
  const FirmwareDownloadException(this.reason);
  final String reason;
  @override
  String toString() => 'FirmwareDownloadException: $reason';
}

/// Channels mirror the LSIG footer's channel field. CI builds `stable`
/// on tagged release pushes and `beta` on pushes to the `beta` branch.
enum FirmwareChannel {
  stable,
  beta,
}

/// Where to fetch from. The default constant points at the production
/// repo's "latest" + "beta" release endpoints. Tests can swap to a
/// fake-server URL via the optional [endpointOverride].
///
/// SECURITY (audit sec-H4, deferred): we fetch via plain `HttpClient`
/// with `followRedirects = true` and no TLS pinning. A captive portal
/// or corporate MITM with a trusted CA can serve any binary. The
/// Ed25519 verify in `firmware_signature_verifier.dart` is the real
/// defense — a forged binary fails verification before any bytes
/// reach the lamp. Adding TLS pinning would be defense-in-depth but
/// drags certificate-rotation maintenance and risks breakage on
/// GitHub-edge cert drift. Tracked in
/// docs/accepted-security-threats.md.
class FirmwareReleaseClient {
  FirmwareReleaseClient({HttpClient? httpClient, this.endpointOverride})
      : _httpClient = httpClient ?? HttpClient();

  final HttpClient _httpClient;
  final String? endpointOverride;

  /// Public repo. The per-variant asset name is composed at request time —
  /// each lamp variant publishes its own `lamp-firmware-<variant>-signed.bin`
  /// asset on the same release. Names MUST match the `release.yml` /
  /// `release-beta.yml` rename step.
  static const String _githubOrg  = 'wearelamplit';
  static const String _githubRepo = 'lamp-os';

  static String _assetNameFor(String lampType) =>
      'lamp-firmware-$lampType-signed.bin';

  /// Resolves the GitHub Releases download URL for the requested
  /// [channel] + [lampType] combination.
  ///   stable → `/releases/latest/download/lamp-firmware-<type>-signed.bin`
  ///   beta   → `/releases/download/beta/lamp-firmware-<type>-signed.bin`
  String _resolveUrl(FirmwareChannel channel, String lampType) {
    final override = endpointOverride;
    if (override != null) return override;
    final asset = _assetNameFor(lampType);
    const base = 'https://github.com/$_githubOrg/$_githubRepo/releases';
    switch (channel) {
      case FirmwareChannel.stable:
        return '$base/latest/download/$asset';
      case FirmwareChannel.beta:
        return '$base/download/beta/$asset';
    }
  }

  /// Fetch the signed firmware blob for [channel] + [lampType]. Follows
  /// redirects (GitHub's release-asset URLs redirect through the S3-backed
  /// objects.githubusercontent.com host). Throws
  /// [FirmwareDownloadException] on any HTTP / IO / size sanity error.
  Future<Uint8List> fetchLatest(
    FirmwareChannel channel, {
    required String lampType,
  }) async {
    final url = _resolveUrl(channel, lampType);
    final uri = Uri.parse(url);
    try {
      final req = await _httpClient.getUrl(uri);
      req.followRedirects = true;
      // Polite UA so 403s from GH's WAF would surface in their logs
      // with our app identifying itself rather than a generic Dart UA.
      req.headers.set(HttpHeaders.userAgentHeader, 'lamp-os-app/1.0');
      final resp = await req.close();
      if (resp.statusCode != HttpStatus.ok) {
        throw FirmwareDownloadException(
            'HTTP ${resp.statusCode} from $url');
      }
      // Accumulate. ~1.5 MB; fits comfortably in phone memory.
      final builder = BytesBuilder(copy: false);
      await for (final chunk in resp) {
        builder.add(chunk);
      }
      final bytes = builder.toBytes();
      // Sanity floor: a signed firmware is at least LSIG(96 B) +
      // bootloader(32 B) + something. A real lamp image is ~1.3 MB. 4 KB
      // is a tiny floor that catches 404 HTML / empty redirects without
      // false-rejecting a hypothetical tiny test image.
      if (bytes.length < 4096) {
        throw FirmwareDownloadException(
            'image suspiciously small (${bytes.length} bytes) — '
            'maybe a redirected HTML error page?');
      }
      return bytes;
    } on SocketException catch (e) {
      throw FirmwareDownloadException('network error: ${e.message}');
    } on HttpException catch (e) {
      throw FirmwareDownloadException('http error: ${e.message}');
    }
  }

  /// Best-effort cleanup; safe to call multiple times. The HttpClient is
  /// otherwise persistent for the life of the provider.
  void close() {
    _httpClient.close(force: false);
  }
}
