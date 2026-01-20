// Package fixtures provides embedded fixtures for tests.
package fixtures

import (
	"embed"
)

//go:embed iperf-server.yaml iperf-client.yaml perf-client.yaml
var fs embed.FS

// ReadIperfServer reads the iperf server pod fixture.
func ReadIperfServer() ([]byte, error) {
	return fs.ReadFile("iperf-server.yaml")
}

// ReadIperfClient reads the iperf client pod fixture.
func ReadIperfClient() ([]byte, error) {
	return fs.ReadFile("iperf-client.yaml")
}

// ReadPerfClient reads the perf client job fixture.
func ReadPerfClient() ([]byte, error) {
	return fs.ReadFile("perf-client.yaml")
}
