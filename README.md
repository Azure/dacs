# DACS

<!-- DACS maintainers: replace the bracketed guidance with real content before going public. -->

DACS (Distributed Accelerator for Cloud Storage) is a high-performance
distributed caching system for Kubernetes that
accelerates storage access for data-intensive workloads. It runs a cache tier
close to compute and transparently intercepts storage I/O so applications get
fast, local-like reads without changing their code.

## Features

- [Key capability 1]
- [Key capability 2]
- [Key capability 3]

## Architecture

DACS is made up of a few cooperating components:

- **CacheServer** - the node-local cache daemon that stores and serves cached data.
- **CacheClient** - the client library applications and the interception layer use
  to talk to CacheServers.
- **Storage interception** - transparently redirects application storage I/O
  through the cache.
- **cache-controller** - a Kubernetes controller that manages the lifecycle of
  cache resources.
- **Scheduler** - a custom scheduler that places CacheServer pods onto cache nodes.

<!-- TODO: add an architecture diagram. -->

## Getting started

### Prerequisites

- [e.g., Kubernetes 1.2x+, a container registry, Helm 3, Docker]

### Build

```bash
make build                      # Debug build
make build BUILD_TYPE=Release   # Release build
make test                       # Run unit tests
```

### Install

```bash
# Example Helm install - update to match the published chart/registry
helm install cache-server ./helm/cache-server
```

## Usage

[Minimal end-to-end example showing how a user deploys and benefits from DACS.]

## Contributing

This project welcomes contributions and suggestions. Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit [https://cla.opensource.microsoft.com](https://cla.opensource.microsoft.com).

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

See [CONTRIBUTING.md](CONTRIBUTING.md) for build and pull request guidance.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Security

See [SECURITY.md](SECURITY.md) for how to report security vulnerabilities. Please
do not report security vulnerabilities through public GitHub issues.

## Support

See [SUPPORT.md](SUPPORT.md) for how to get help.

## License

This project is licensed under the [Apache License 2.0](LICENSE).

## Data Collection

The software may collect information about you and your use of the software and
send it to Microsoft. Microsoft may use this information to provide services and
improve our products and services. You may turn off the telemetry as described
in the repository. There are also some features in the software that may enable
you and Microsoft to collect data from users of your applications. If you use
these features, you must comply with applicable law, including providing
appropriate notices to users of your applications together with a copy of
Microsoft's privacy statement. Our privacy statement is located at
https://go.microsoft.com/fwlink/?LinkID=824704. You can learn more about data
collection and use in the help documentation and our privacy statement. Your use
of the software operates as your consent to these practices.

### What DACS collects and how to turn it off

DACS does not transmit any telemetry to Microsoft. The only data it produces is
local [Prometheus](https://prometheus.io/) metrics that you scrape from your own
cluster:

- **cache-controller** metrics endpoint is **disabled by default**
  (`--metrics-bind-address=0`). It is only served when you set a bind address.
- **CacheServer** exposes metrics on a configurable port; remove or firewall the
  configured metrics port to disable the endpoint.

No metric data leaves your environment unless you configure your own Prometheus
or scraping infrastructure to collect it.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft
trademarks or logos is subject to and must follow
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.
