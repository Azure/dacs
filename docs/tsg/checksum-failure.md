# DACS Checksum Validation Failure

## Symptoms

Reads fail with checksum errors or `CHECKSUM_MISMATCH`, or the checksum-enabled
smoke-test download fails.

## Description

Checksum validation can detect mismatched data or missing checksum metadata
under a strict policy. Cache checksums and optional remote Blob transfer
checksums are separate mechanisms. A mismatch must be investigated; turning
validation off does not establish that the returned data is correct.

## Steps to Mitigate

1. Stop using suspect output for downstream processing. Record the affected
   object/version or cache key, request range, client/server versions, and time.
   Preserve logs and avoid broad cache deletion.
2. Collect affected client and server logs with sensitive paths redacted:

   ```bash
   export NAMESPACE="dacs-system"
   export CLIENT_NAMESPACE="application-namespace"
   kubectl logs <cache-pod> -n "$NAMESPACE" -c cacheserver --tail=200
   kubectl logs <client-pod> -n "$CLIENT_NAMESPACE" -c <client-container> --tail=200
   ```

3. Review the actual client configuration. StorageIntercept settings include
   `cacheCalculateChecksumEnabled`, `cacheValidateChecksumEnabled`,
   `cacheChecksumAlgorithm`, and `cacheChecksumMetadataPolicy`. A `required`
   metadata policy reports missing metadata as a mismatch; distinguish that from
   a checksum value mismatch. Align writers/readers with the intended policy,
   rather than weakening checks to read older entries.
4. Check for source changes or concurrent writes. Reproduce using an unchanged
   test object and a unique key prefix so a different source version is not
   mistaken for the same expected content.
5. Independently validate the authoritative remote object's contents through an
   approved storage client. If the source is correct and a cached entry is
   suspect, escalate for scoped remediation/repopulation; do not erase an entire
   cache that may contain cache-only or pending writes.
6. For remote SDK downloads, review `remoteBlobChecksumEnabled` and
   `remoteBlobChecksumRetryMaxAttempts`. Persistent errors after bounded retries
   need investigation, not unlimited retries or disabled validation. See
   [checksum configuration](../../src/subsystems/storage/storage_intercept/README.md).

## Verification Steps

Repeat the affected read with validation enabled and compare it with the
authoritative expected content. Run the
[checksum-enabled smoke test](../quickstart.md#run-the-end-to-end-smoke-test)
with a new prefix; both Jobs must complete with
`All iterations succeeded: true`. A passing isolated smoke test does not by
itself resolve a mismatch for the original object.

## Contact Information

Use [SUPPORT.md](../../SUPPORT.md) with sanitized mismatch details and the
[escalation checklist](../installation-troubleshooting.md#escalation-checklist).
For suspected vulnerabilities, follow [SECURITY.md](../../SECURITY.md) privately.
