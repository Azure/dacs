// Package fixtures provides embedded fixtures for tests.
package fixtures

import (
	"embed"
)

// IperfServerYAML is the path to the iperf server pod fixture.
const IperfServerYAML = "iperf-server.yaml"

// IperfClientYAML is the path to the iperf client pod fixture.
const IperfClientYAML = "iperf-client.yaml"

// PerfClientYAML is the path to the perf client job fixture.
const PerfClientYAML = "perf-client.yaml"

// DACSCacheClientYAML is the path to the dacs cache client job fixture.
const DACSCacheClientYAML = "dacs_cacheclient_job.yaml"

// DACSAdapterBenchmarkYAML is the path to the dacs adapter benchmark job fixture.
const DACSAdapterBenchmarkYAML = "dacs_adapter_benchmark_job.yaml"

// DACSClientPodYAML is the path to the dacs client pod fixture.
const DACSClientPodYAML = "dacs-client-pod.yaml"

//go:embed iperf-server.yaml iperf-client.yaml perf-client.yaml dacs_cacheclient_job.yaml perf-client-serviceaccount.yaml mpi-job.yaml dacs_adapter_benchmark_job.yaml dacs-client-pod.yaml
var fs embed.FS

// ReadIperfServer reads the iperf server pod fixture.
func ReadIperfServer() ([]byte, error) {
	return fs.ReadFile(IperfServerYAML)
}

// ReadIperfClient reads the iperf client pod fixture.
func ReadIperfClient() ([]byte, error) {
	return fs.ReadFile(IperfClientYAML)
}

// ReadPerfClient reads the perf client job fixture.
func ReadPerfClient() ([]byte, error) {
	return fs.ReadFile(PerfClientYAML)
}

// ReadDACSCacheClientJob reads the dacs cache client job fixture.
func ReadDACSCacheClientJob() ([]byte, error) {
	return fs.ReadFile(DACSCacheClientYAML)
}

// ReadPerfClientServiceAccount reads the perf client service account fixture for workload identity.
func ReadPerfClientServiceAccount() ([]byte, error) {
	return fs.ReadFile("perf-client-serviceaccount.yaml")
}

// ReadMPIJob reads the MPIJob from fixture
func ReadMPIJob() ([]byte, error) {
	return fs.ReadFile("mpi-job.yaml")
}

// ReadDACSAdapterBenchmarkJob reads the dacs adapter benchmark job fixture.
func ReadDACSAdapterBenchmarkJob() ([]byte, error) {
	return fs.ReadFile(DACSAdapterBenchmarkYAML)
}

// ReadDACSClientPod reads the dacs client pod fixture.
func ReadDACSClientPod() ([]byte, error) {
	return fs.ReadFile(DACSClientPodYAML)
}
