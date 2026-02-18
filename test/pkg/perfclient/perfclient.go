// Package perfclient provides configuration and job creation utilities
// for the performance test client.
package perfclient

import (
	"fmt"
	"os"

	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/cluster"
	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
)

// ChecksumAlgorithm represents a supported checksum algorithm.
type ChecksumAlgorithm string

const (
	// ChecksumAlgorithmXXHash32 is the xxhash32 checksum algorithm.
	ChecksumAlgorithmXXHash32 ChecksumAlgorithm = "xxhash32"
	// ChecksumAlgorithmCRC32 is the crc32 checksum algorithm.
	ChecksumAlgorithmCRC32 ChecksumAlgorithm = "crc32"
)

// Mode represents the operation mode for the perf client.
type Mode string

const (
	// ModeUpload is the upload operation mode.
	ModeUpload Mode = "upload"
	// ModeDownload is the download operation mode.
	ModeDownload Mode = "download"
)

// ClientType represents the type of client to use for the perf test.
type ClientType string

const (
	// ClientTypeCache is the cache server client (default).
	ClientTypeCache ClientType = "cache"
	// ClientTypeDirectBlob is the direct blob storage client.
	ClientTypeDirectBlob ClientType = "direct-blob"
)

// Config contains configuration for the perf client job.
type Config struct {
	// ClientType is the type of client (cache or direct-blob)
	ClientType ClientType
	// Mode is the operation mode (upload or download)
	Mode Mode
	// Namespace is the Kubernetes namespace
	Namespace string
	// Image is the full container image (registry/repo:tag)
	Image string
	// ServerHostname is the cache server hostname(s), separated by #
	ServerHostname string
	// NumServers is the number of cache servers (for metrics reporting)
	NumServers int
	// NumThreads is the number of threads
	NumThreads int
	// DataSizeGiB is the data size in GiB
	DataSizeGiB int
	// NumIterations is the number of iterations
	NumIterations int
	// Prefix is the prefix for test data
	Prefix string
	// ChunkSizeBytes is the chunk size in bytes (default 33554432 = 32 MiB)
	ChunkSizeBytes int
	// EnableCalculateChecksum enables checksum calculation on upload
	EnableCalculateChecksum bool
	// EnableValidateChecksum enables checksum validation on download
	EnableValidateChecksum bool
	// ChecksumAlgorithm is the checksum algorithm to use
	ChecksumAlgorithm ChecksumAlgorithm
	// ColocateWithServer schedules the perf client on the same node as the cache server
	ColocateWithServer bool

	// Direct blob client options (used when ClientType is ClientTypeDirectBlob)
	// StorageAccount is the Azure storage account name
	StorageAccount string
	// StorageContainer is the Azure storage container name
	StorageContainer string
	// WorkloadIdentityClientID is the client ID for WorkloadIdentityCredential authentication
	WorkloadIdentityClientID string
	// ServiceAccountName is the Kubernetes service account to use for workload identity
	ServiceAccountName string
}

// NewConfig creates a new Config with default values.
func NewConfig() *Config {
	return &Config{
		ClientType:              ClientTypeCache,
		Mode:                    ModeUpload,
		Namespace:               "cache-server",
		Image:                   common.GetDefaultImage(),
		ServerHostname:          "cacheserver-0.cacheserver.cache-server.svc.cluster.local",
		NumThreads:              1,
		DataSizeGiB:             1,
		NumIterations:           1,
		Prefix:                  "test",
		ChunkSizeBytes:          33554432, // 32 MiB default
		EnableCalculateChecksum: false,
		EnableValidateChecksum:  false,
		ChecksumAlgorithm:       ChecksumAlgorithmXXHash32,
	}
}

// WithClientType sets the client type (cache or direct-blob).
func (c *Config) WithClientType(clientType ClientType) *Config {
	c.ClientType = clientType
	return c
}

// WithMode sets the operation mode (upload or download).
func (c *Config) WithMode(mode Mode) *Config {
	c.Mode = mode
	return c
}

// WithNamespace sets the Kubernetes namespace.
func (c *Config) WithNamespace(namespace string) *Config {
	c.Namespace = namespace
	return c
}

// WithImage sets the container image.
func (c *Config) WithImage(image string) *Config {
	c.Image = image
	return c
}

// WithServerHostname sets the cache server hostname(s).
func (c *Config) WithServerHostname(hostname string) *Config {
	c.ServerHostname = hostname
	return c
}

// WithNumServers sets the number of cache servers (for metrics reporting).
func (c *Config) WithNumServers(numServers int) *Config {
	c.NumServers = numServers
	return c
}

// WithNumThreads sets the number of threads.
func (c *Config) WithNumThreads(numThreads int) *Config {
	c.NumThreads = numThreads
	return c
}

// WithDataSizeGiB sets the data size in GiB.
func (c *Config) WithDataSizeGiB(dataSizeGiB int) *Config {
	c.DataSizeGiB = dataSizeGiB
	return c
}

// WithNumIterations sets the number of iterations.
func (c *Config) WithNumIterations(numIterations int) *Config {
	c.NumIterations = numIterations
	return c
}

// WithPrefix sets the prefix for test data.
func (c *Config) WithPrefix(prefix string) *Config {
	c.Prefix = prefix
	return c
}

// WithChunkSizeBytes sets the chunk size in bytes.
func (c *Config) WithChunkSizeBytes(chunkSizeBytes int) *Config {
	c.ChunkSizeBytes = chunkSizeBytes
	return c
}

// WithCalculateChecksum enables or disables checksum calculation on upload.
func (c *Config) WithCalculateChecksum(enable bool) *Config {
	c.EnableCalculateChecksum = enable
	return c
}

// WithValidateChecksum enables or disables checksum validation on download.
func (c *Config) WithValidateChecksum(enable bool) *Config {
	c.EnableValidateChecksum = enable
	return c
}

// WithChecksumAlgorithm sets the checksum algorithm.
func (c *Config) WithChecksumAlgorithm(algorithm ChecksumAlgorithm) *Config {
	c.ChecksumAlgorithm = algorithm
	return c
}

// WithColocate sets whether to co-locate the perf client on the same node as the cache server.
func (c *Config) WithColocate(colocate bool) *Config {
	c.ColocateWithServer = colocate
	return c
}

// WithStorageAccount sets the Azure storage account name for direct blob client.
func (c *Config) WithStorageAccount(account string) *Config {
	c.StorageAccount = account
	return c
}

// WithStorageContainer sets the Azure storage container name for direct blob client.
func (c *Config) WithStorageContainer(container string) *Config {
	c.StorageContainer = container
	return c
}

// WithWorkloadIdentity configures the job to use Azure Workload Identity for authentication.
// The clientID is the Azure managed identity client ID, and serviceAccountName is the
// Kubernetes ServiceAccount annotated with azure.workload.identity/client-id.
func (c *Config) WithWorkloadIdentity(clientID string, serviceAccountName string) *Config {
	c.WorkloadIdentityClientID = clientID
	c.ServiceAccountName = serviceAccountName
	return c
}

// Description returns a human-readable description of the job configuration.
func (c *Config) Description() string {
	if c.ClientType == ClientTypeDirectBlob {
		return fmt.Sprintf("Creating direct blob %s client job (storage=%s, container=%s, auth=EntraID)",
			c.Mode, c.StorageAccount, c.StorageContainer)
	}
	return fmt.Sprintf("Creating %s perf client job (checksum calc=%t, validate=%t, colocate=%t)",
		c.Mode, c.EnableCalculateChecksum, c.EnableValidateChecksum, c.ColocateWithServer)
}

// getDefaultImage returns the default perf client image based on environment variables
// matching the helm chart configuration. On minikube, uses local image.
func getDefaultImage() string {
	// On minikube, use the local image that was loaded via `minikube image load`
	if cluster.IsMinikube() {
		return "cache-test-client:local"
	}

	registry := os.Getenv("DOCKER_REGISTRY")
	if registry == "" {
		registry = "docker.io"
	}
	buildNumber := os.Getenv("BUILD_NUMBER")
	if buildNumber == "" {
		buildNumber = "latest"
	}

	return fmt.Sprintf("%s/cache-test-client:%s", registry, buildNumber)
}

// LoadJob loads the perf client job from the embedded fixture
// and applies the provided configuration by modifying the parsed struct.
func (c *Config) LoadJob() (*batchv1.Job, error) {
	// Validate direct-blob specific requirements
	if c.ClientType == ClientTypeDirectBlob {
		if c.StorageAccount == "" {
			return nil, fmt.Errorf("StorageAccount must be set for direct-blob client (use --storage-account flag)")
		}
		if c.StorageContainer == "" {
			return nil, fmt.Errorf("StorageContainer must be set for direct-blob client (use --storage-container flag)")
		}
		if c.WorkloadIdentityClientID == "" {
			return nil, fmt.Errorf("WorkloadIdentityClientID must be set for direct-blob client (use --workload-identity-client-id flag)")
		}
		if c.ServiceAccountName == "" {
			return nil, fmt.Errorf("ServiceAccountName must be set for direct-blob client (use --workload-identity-sa flag)")
		}
	}

	data, err := fixtures.ReadPerfClient()
	if err != nil {
		return nil, fmt.Errorf("failed to read perf client fixture: %w", err)
	}

	// Unmarshal base YAML into Job struct
	var job batchv1.Job
	if err := yaml.Unmarshal(data, &job); err != nil {
		return nil, fmt.Errorf("failed to unmarshal perf client job: %w", err)
	}

	// Apply configuration to the job — include the prefix in the job name
	// to ensure uniqueness across test configs, preventing Kubernetes TTL
	// controller from deleting a new job when a previous same-named job's TTL expires.
	jobName := string(c.Mode) + "-client-" + c.Prefix
	if c.ClientType == ClientTypeDirectBlob {
		jobName = "direct-blob-" + string(c.Mode) + "-client-" + c.Prefix
	}
	job.Name = jobName
	job.Namespace = c.Namespace

	// Update job labels
	if job.Labels == nil {
		job.Labels = make(map[string]string)
	}
	job.Labels["dacs.azure.com/perf-mode"] = string(c.Mode)
	job.Labels["dacs.azure.com/client-type"] = string(c.ClientType)

	// Update selector labels (use perf-mode so each mode has a unique selector;
	// app=perf-client stays constant for kubectl-prof --selector)
	job.Spec.Selector.MatchLabels["dacs.azure.com/perf-mode"] = string(c.Mode)

	// Update pod template labels - include test-pod label for support bundle collection
	job.Spec.Template.Labels["dacs.azure.com/test-pod"] = "true"
	job.Spec.Template.Labels["dacs.azure.com/perf-mode"] = string(c.Mode)

	// Configure workload identity if specified
	if c.WorkloadIdentityClientID != "" && c.ServiceAccountName != "" {
		// Add the workload identity label to enable the mutating webhook
		job.Spec.Template.Labels["azure.workload.identity/use"] = "true"
		// Set the service account
		job.Spec.Template.Spec.ServiceAccountName = c.ServiceAccountName
	}

	// Handle affinity configuration based on ColocateWithServer setting
	if c.ColocateWithServer {
		// Replace anti-affinity with affinity to schedule on same node as cacheserver-0
		job.Spec.Template.Spec.Affinity.PodAntiAffinity = nil
		job.Spec.Template.Spec.Affinity.PodAffinity = &corev1.PodAffinity{
			RequiredDuringSchedulingIgnoredDuringExecution: []corev1.PodAffinityTerm{
				{
					LabelSelector: &metav1.LabelSelector{
						MatchExpressions: []metav1.LabelSelectorRequirement{
							{
								Key:      "statefulset.kubernetes.io/pod-name",
								Operator: metav1.LabelSelectorOpIn,
								Values:   []string{"cacheserver-0"},
							},
						},
					},
					TopologyKey: "kubernetes.io/hostname",
				},
			},
		}
	} else {
		// Update pod anti-affinity for the client pods (default behavior)
		if job.Spec.Template.Spec.Affinity != nil &&
			job.Spec.Template.Spec.Affinity.PodAntiAffinity != nil {
			for i := range job.Spec.Template.Spec.Affinity.PodAntiAffinity.PreferredDuringSchedulingIgnoredDuringExecution {
				pref := &job.Spec.Template.Spec.Affinity.PodAntiAffinity.PreferredDuringSchedulingIgnoredDuringExecution[i]
				for j := range pref.PodAffinityTerm.LabelSelector.MatchExpressions {
					expr := &pref.PodAffinityTerm.LabelSelector.MatchExpressions[j]
					if expr.Key == "dacs.azure.com/perf-mode" {
						expr.Values = []string{string(c.Mode)}
					}
				}
			}
		}
	}

	// Update container image and args
	if len(job.Spec.Template.Spec.Containers) > 0 {
		container := &job.Spec.Template.Spec.Containers[0]
		container.Image = c.Image

		// Build the args array with config values
		container.Args = []string{
			"--server-port", "9065",
			"--server-hostname", c.ServerHostname,
			"--num-threads", fmt.Sprintf("%d", c.NumThreads),
			"--data-size-GiB", fmt.Sprintf("%d", c.DataSizeGiB),
			"--prefix", c.Prefix,
			"--mode", string(c.Mode),
			"--num-iterations", fmt.Sprintf("%d", c.NumIterations),
			"--ib-enabled", "false",
			"--chunk-size-bytes", fmt.Sprintf("%d", c.ChunkSizeBytes),
		}

		// Set image pull policy: Always for AKS easier for when perfclient image
		// is changed frequently, but use IfNotPresent for minikube to work with locally loaded images
		if !cluster.IsMinikube() {
			container.ImagePullPolicy = corev1.PullAlways
		}

		// Build the args array based on client type
		if c.ClientType == ClientTypeDirectBlob {
			// Direct blob client args - uses Entra ID / Workload Identity for auth
			container.Args = []string{
				"--direct-blob-client",
				"--storage-account", c.StorageAccount,
				"--container", c.StorageContainer,
				"--num-threads", fmt.Sprintf("%d", c.NumThreads),
				"--data-size-GiB", fmt.Sprintf("%d", c.DataSizeGiB),
				"--prefix", c.Prefix,
				"--mode", string(c.Mode),
				"--num-iterations", fmt.Sprintf("%d", c.NumIterations),
			}
		} else {
			// Cache client args (default)
			container.Args = []string{
				"--server-port", "9065",
				"--server-hostname", c.ServerHostname,
				"--num-threads", fmt.Sprintf("%d", c.NumThreads),
				"--data-size-GiB", fmt.Sprintf("%d", c.DataSizeGiB),
				"--prefix", c.Prefix,
				"--mode", string(c.Mode),
				"--num-iterations", fmt.Sprintf("%d", c.NumIterations),
				"--ib-enabled", "false",
			}

			// Only add checksum args if checksum is enabled
			if c.EnableCalculateChecksum || c.EnableValidateChecksum {
				container.Args = append(container.Args,
					"--enable-calculate-checksum", fmt.Sprintf("%t", c.EnableCalculateChecksum),
					"--enable-validate-checksum", fmt.Sprintf("%t", c.EnableValidateChecksum),
					"--checksum-algorithm", string(c.ChecksumAlgorithm),
				)
			}
		}
	}

	return &job, nil
}

// JobYAML returns the perf client job as YAML bytes with configuration applied.
func (c *Config) JobYAML() ([]byte, error) {
	job, err := c.LoadJob()
	if err != nil {
		return nil, err
	}
	return yaml.Marshal(job)
}

// LoadServiceAccount loads the perf client service account from the embedded fixture
// and applies the workload identity client ID annotation.
// Returns nil if workload identity is not configured.
func (c *Config) LoadServiceAccount() (*corev1.ServiceAccount, error) {
	if c.WorkloadIdentityClientID == "" || c.ServiceAccountName == "" {
		return nil, fmt.Errorf("both WorkloadIdentityClientID and ServiceAccountName must be set to load service account")
	}

	data, err := fixtures.ReadPerfClientServiceAccount()
	if err != nil {
		return nil, fmt.Errorf("failed to read service account fixture: %w", err)
	}

	var sa corev1.ServiceAccount
	if err := yaml.Unmarshal(data, &sa); err != nil {
		return nil, fmt.Errorf("failed to unmarshal service account: %w", err)
	}

	// Apply configuration
	sa.Name = c.ServiceAccountName
	sa.Namespace = c.Namespace

	// Set the workload identity client ID annotation
	if sa.Annotations == nil {
		sa.Annotations = make(map[string]string)
	}
	sa.Annotations["azure.workload.identity/client-id"] = c.WorkloadIdentityClientID

	return &sa, nil
}

// ServiceAccountYAML returns the service account as YAML bytes with configuration applied.
// Returns nil if workload identity is not configured.
func (c *Config) ServiceAccountYAML() ([]byte, error) {
	sa, err := c.LoadServiceAccount()
	if err != nil {
		return nil, err
	}
	return yaml.Marshal(sa)
}
