// Package fixture provides embedded fixtures for the perf client.
package fixture

import (
	"embed"
)

//go:embed perf-client.yaml
var fs embed.FS

// PerfClientYAML is the path to the perf client job fixture.
const PerfClientYAML = "perf-client.yaml"

// ReadPerfClientJob reads the perf client job fixture.
func ReadPerfClientJob() ([]byte, error) {
	return fs.ReadFile(PerfClientYAML)
}
