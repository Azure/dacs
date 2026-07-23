# Contributing

This project welcomes contributions and suggestions. Most contributions require you to
agree to a Contributor License Agreement (CLA) declaring that you have the right to,
and actually do, grant us the rights to use your contribution. For details, visit
https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need
to provide a CLA and decorate the PR appropriately (e.g., status check, comment). Simply
follow the instructions provided by the bot. You will only need to do this once across
all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/)
or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Building and testing

DACS builds from a fresh clone using standard tools (Docker, Make). See the
[README](README.md) for prerequisites and the canonical command list.

- Build (debug): `make build`
- Build (release): `make build BUILD_TYPE=Release`
- Run all unit tests: `make test`
- C++ tests only: `make test-native`
- Go tests only: `make test-go`

## Pull request expectations

- Keep PRs focused and reasonably small; explain the motivation in the description.
- Ensure the build and unit tests pass before requesting review.
- New features should include tests.
- Be responsive to review feedback. A maintainer must not merge a PR until the
  `license/cla` status check reports that all CLA requirements are met.
