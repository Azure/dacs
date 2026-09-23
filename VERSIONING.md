# DACS versioning and releases

DACS uses [Semantic Versioning 2.0.0](https://semver.org/). Git tags are the
source of truth for release and development versions. The initial release line
is `v0.1.0`.

Major version zero indicates initial development. Public APIs, configuration,
and behavior may change between minor releases until DACS reaches `v1.0.0`.

## Version source

Build automation derives the version from the nearest reachable Git tag:

```bash
git describe --tags --match='v*' --abbrev=14 HEAD
```

If no matching tag is reachable, the version uses the initial release line,
the total commit count, and the abbreviated commit ID:

```text
v0.1.0-alpha.0.<commit-count>+<commit-id>
```

For example, commit 42 with ID `a1b2c3d4e5f6a7` produces
`v0.1.0-alpha.0.42+a1b2c3d4e5f6a7`. This bootstrap version is always a
development version. Stable publication requires an exact stable tag on the
release commit.

Builds must fetch the Git tags and history required by `git describe`.

DACS uses the `v` prefix unless an ecosystem does not use it:

- Git tags, source versions, container tags, and Helm `appVersion` retain `v`.
- Helm chart, Python package, and Rust package versions omit `v`.

## Version formats

An exact tag is the version of its commit:

```text
v0.1.0
v0.1.0-rc.1
v0.2.0-alpha.0
```

For an untagged commit, `git describe` adds the number of commits since the
nearest tag and an abbreviated commit ID. For example, six commits after
`v0.2.0-alpha.0` produces:

```text
v0.2.0-alpha.0-6-ga1b2c3d4e5f6a7
```

Build automation normalizes this to:

```text
v0.2.0-alpha.0.6+a1b2c3d4e5f6a7
```

The commit distance determines prerelease ordering. The commit ID identifies
the source without affecting version precedence.

| Artifact | Stable | Release candidate | Development |
| --- | --- | --- | --- |
| Git tag | `v0.1.0` | `v0.1.0-rc.1` | Nearest prerelease tag |
| Source version | `v0.1.0` | `v0.1.0-rc.1` | `v0.2.0-alpha.0.6+a1b2c3d4e5f6a7` |
| Container tag | `v0.1.0` | `v0.1.0-rc.1` | `v0.2.0-alpha.0.6_a1b2c3d4e5f6a7` |
| Helm `version` | `0.1.0` | `0.1.0-rc.1` | `0.2.0-alpha.0.6+a1b2c3d4e5f6a7` |
| Helm `appVersion` | `v0.1.0` | `v0.1.0-rc.1` | `v0.2.0-alpha.0.6+a1b2c3d4e5f6a7` |
| Python package | `0.1.0` | `0.1.0rc1` | `0.2.0a0.dev6+a1b2c3d4e5f6a7` |
| Rust package | `0.1.0` | `0.1.0-rc.1` | `0.2.0-alpha.0.6+a1b2c3d4e5f6a7` |
| Azure Universal Package | `0.1.0` | `0.1.0-rc.1` | `0.2.0-alpha.0.6.ga1b2c3d4e5f6a7` |

OCI tags do not allow `+`, so container tags replace it with `_`. Helm performs
the same conversion when storing chart versions in an OCI registry. Python
uses the equivalent PEP 440 syntax. Azure Universal Package versions preserve
the development commit by replacing `+` with `.g`, which keeps the SHA in an
alphanumeric prerelease identifier.

## Select a release line

The first prerelease tag identifies the intended next release:

- Use a patch line, such as `v0.1.1-alpha.0`, for backward-compatible fixes and
  maintenance after `v0.1.0`.
- Use a minor line, such as `v0.2.0-alpha.0`, for features or changes to public
  APIs, configuration, deployment contracts, or observable behavior.
- Use `v1.0.0` when DACS declares its public interfaces stable.

Advance a release with immutable prerelease tags:

```text
v0.2.0-alpha.0
v0.2.0-alpha.1
v0.2.0-beta.0
v0.2.0-rc.1
v0.2.0
```

If planned work no longer fits the selected release line, start the correct
line with a new prerelease tag. Do not move an existing tag.

## Publication

Development and prerelease container images are published to the DACS
development registry:

```text
dacstest.azurecr.io/dacs/cache-server:<derived-version>
dacstest.azurecr.io/dacs/cache-controller:<derived-version>
```

Development and prerelease Helm charts use the same repository layout:

```text
oci://dacstest.azurecr.io/dacs/helmchart/dacs:<derived-version>
oci://dacstest.azurecr.io/dacs/helmchart/dacs-prereq:<derived-version>
```

The development registry mirrors the MCR path beneath the registry host.
Promoting an artifact therefore changes `dacstest.azurecr.io` to
`mcr.microsoft.com` without changing its repository path. Development and
prerelease artifacts are not published to MCR.

An approved stable release publishes these multi-platform images:

```text
mcr.microsoft.com/dacs/cache-server:v0.1.0
mcr.microsoft.com/dacs/cache-controller:v0.1.0
```

It also publishes these Helm charts:

```text
oci://mcr.microsoft.com/dacs/helmchart/dacs:0.1.0
oci://mcr.microsoft.com/dacs/helmchart/dacs-prereq:0.1.0
```

The images support `linux/amd64` and `linux/arm64`. Release automation promotes
the validated image digests from the development registry instead of
rebuilding them.

Official builds retain validated native packages, architecture-specific AKS
Ginkgo test binaries, Python wheels, and a unified Rust package source bundle
as pipeline artifacts. The Python distribution name is `dacs-client`; the
Cargo package name is `dacs-rust-client`.

Architecture-specific Azure Universal Package names are:

```text
dacs-storage-amd64
dacs-storage-arm64
dacs-storage-cache-client-amd64
dacs-storage-cache-client-arm64
dacs-aks-tests-amd64
dacs-aks-tests-arm64
```

## Release process

1. Select the patch, minor, or major release line.
2. Create its first prerelease tag.
3. Build and validate artifacts using the version derived from Git.
4. Advance the release with `alpha`, `beta`, and `rc` tags as needed.
5. Create the stable tag on the approved commit.
6. Promote the validated image digests and chart packages without rebuilding.
7. Retain the native packages, AKS Ginkgo test binaries, Python wheels, and
   unified Rust package source bundle as pipeline artifacts.
8. Record the source tag, commit, coordinates, and digests in release metadata.

All artifacts in a release derive from the same Git tag and commit. Release
automation must fail if their provenance differs.

## References

- [Semantic Versioning 2.0.0](https://semver.org/)
- [Kubernetes version management](https://github.com/kubernetes/kubernetes/blob/master/hack/lib/version.sh)
- [Helm chart format](https://helm.sh/docs/topics/charts/)
- [Python version specifiers](https://packaging.python.org/en/latest/specifications/version-specifiers/)
- [Cargo manifest package metadata](https://doc.rust-lang.org/cargo/reference/manifest.html#the-version-field)
