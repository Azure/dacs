package e2e

import (
	"context"
	"encoding/xml"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/jstemmer/go-junit-report/v2/junit"
	. "github.com/onsi/ginkgo/v2"
	corev1 "k8s.io/api/core/v1"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/yaml"

	"github.com/Azure/dacs/test/pkg/common"
	"github.com/Azure/dacs/test/pkg/common/fixtures"
	"github.com/Azure/dacs/test/pkg/kube/pod"
	"github.com/Azure/dacs/test/pkg/utils"
)

// TestType represents the type of Python adapter test to run.
type TestType string

const (
	TestTypeBlobFile    TestType = "blobfile_adapter"
	TestTypeBoostedBlob TestType = "boostedblob_adapter"

	dacsPodName                = "dacs-python-client"
	adapterE2EContainerNamePrefix = "adapter-e2e-"
)

// pytestResultCollector accumulates pytest JUnit XML results for writing at the end.
var pytestResultCollector = &junit.Testsuites{}

// DACSAdapterTestConfig holds configuration for dacs adapter e2e tests.
type DACSAdapterTestConfig struct {
	k8sClient                client.Client
	workloadIdentityClientID string
	serviceAccountName       string
	namespace                string
	image                    string
	storageAccount           string
	containerName            string
	clientID                 string
	sasToken                 string
	cacheEnabled             bool
	cacheDiscoveryURL        string
	pytestOutput             string
}

// TestResult holds the result of a Python test run.
type TestResult struct {
	TestType    TestType
	Passed      int
	Failed      int
	Skipped     int
	Errors      int
	Duration    time.Duration
	Output      string
	ExitCode    int
	FailedTests []string
	PassedTests []string
}

// PytestAdapterConfig defines the configuration for a pytest adapter test suite.
type PytestAdapterConfig struct {
	AdapterName string
	TestType    TestType
	Labels      []string
}

// getAdapterTestImage returns the test client image from command-line flag or default.
func getAdapterTestImage() string {
	if fluxOpts.ClientImage != "" {
		return fluxOpts.ClientImage
	}
	return common.GetDefaultImage()
}

// deploy deploys the Python client test pod.
func (c *DACSAdapterTestConfig) deploy(ctx context.Context) error {
	// If workload identity is configured, ensure the ServiceAccount exists
	if c.workloadIdentityClientID != "" && c.serviceAccountName != "" {
		By(fmt.Sprintf("Ensuring workload identity ServiceAccount %s exists", c.serviceAccountName))
		saYAML, err := common.WorkloadIdentitySAYAML(c.serviceAccountName, c.namespace, c.workloadIdentityClientID)
		if err != nil {
			return fmt.Errorf("failed to create workload identity SA YAML: %w", err)
		}
		cmd := exec.CommandContext(ctx, "kubectl", "apply", "-f", "-")
		cmd.Stdin = utils.NewStringReader(string(saYAML))
		if output, err := utils.Run(cmd); err != nil {
			return fmt.Errorf("failed to apply workload identity service account: %s: %w", output, err)
		}
	}

	By("Deploying Python client Pod")
	if err := c.deployPod(ctx); err != nil {
		return err
	}

	By("Waiting for Python client Pod to be ready")
	pod.WaitForRunning(ctx, dacsPodName, c.namespace, 5*time.Minute)

	log.Info("Python client pod is ready", "pod", dacsPodName, "namespace", c.namespace)
	return nil
}

// deployPod creates the Pod from fixture with config substitution.
func (c *DACSAdapterTestConfig) deployPod(ctx context.Context) error {
	data, err := fixtures.ReadDACSClientPod()
	if err != nil {
		return fmt.Errorf("failed to read pod fixture: %w", err)
	}

	var p corev1.Pod
	if err := yaml.Unmarshal(data, &p); err != nil {
		return fmt.Errorf("failed to unmarshal pod: %w", err)
	}

	p.Namespace = c.namespace

	// Configure workload identity if set
	if c.workloadIdentityClientID != "" && c.serviceAccountName != "" {
		p.Spec.ServiceAccountName = c.serviceAccountName
		if p.Labels == nil {
			p.Labels = make(map[string]string)
		}
		p.Labels["azure.workload.identity/use"] = "true"
	}

	// Update main container
	for i := range p.Spec.Containers {
		container := &p.Spec.Containers[i]
		container.Image = c.image
		for j := range container.Env {
			env := &container.Env[j]
			switch env.Name {
			case "AZURE_CLIENT_ID":
				if c.workloadIdentityClientID != "" {
					env.Value = c.workloadIdentityClientID
				} else {
					env.Value = c.clientID
				}
			case "AZURE_BLOB_PATH":
				env.Value = c.azureBlobPath(TestTypeBlobFile)
			case "STORAGE_ACCOUNT":
				env.Value = c.storageAccount
			case "TEST_CONTAINER":
				env.Value = c.containerName
			case "CACHE_ENABLED":
				env.Value = fmt.Sprintf("%t", c.cacheEnabled)
			case "CACHE_DISCOVERY_URL":
				if c.cacheEnabled {
					env.Value = c.cacheDiscoveryURL
				}
			case "STORAGE_INTCPT_SAS_TOKEN":
				env.Value = c.sasToken
			case "USE_SAS_TOKEN":
				if c.sasToken != "" {
					env.Value = "true"
				}
			}
		}
	}

	// Delete existing Pod if present
	if err := pod.Delete(ctx, p.Name, p.Namespace); err != nil {
		return fmt.Errorf("failed to delete existing pod: %w", err)
	}

	if err := c.k8sClient.Create(ctx, &p); err != nil {
		return fmt.Errorf("failed to create pod: %w", err)
	}

	return nil
}

// cleanup removes the Python client test pod.
func (c *DACSAdapterTestConfig) cleanup(ctx context.Context) error {
	By("Cleaning up Python client resources")

	if err := pod.Delete(ctx, dacsPodName, c.namespace); err != nil {
		log.Error(err, "Failed to delete pod", "pod", dacsPodName)
	}

	return nil
}

// azureBlobPath returns the Azure blob path for tests.
func (c *DACSAdapterTestConfig) azureBlobPath(testType TestType) string {
	return fmt.Sprintf("az://%s.blob.core.windows.net/%s/dacs_%s", c.storageAccount, c.containerName, testType)
}

// runTests runs the specified Python tests in the pod and returns the result.
func (c *DACSAdapterTestConfig) runTests(ctx context.Context, testType TestType) (*TestResult, error) {
	By(fmt.Sprintf("Running %s tests in pod %s", testType, dacsPodName))

	junitFile := fmt.Sprintf("/tmp/pytest_%s_results.xml", testType)
	azureBlobPath := c.azureBlobPath(testType)

	// Build auth environment for test execution
	authEnv := "export AZURE_USE_IDENTITY=1"
	if c.sasToken != "" {
		authEnv = fmt.Sprintf("export USE_SAS_TOKEN=true\nexport STORAGE_INTCPT_SAS_TOKEN='%s'", c.sasToken)
	}

	testCmd := fmt.Sprintf(`
export AZURE_BLOB_PATH='%s'
export PYTHONPATH=/app:/app/py_dacs_client
export PYTHONUNBUFFERED=1
%s
cd /app/tests
python3 -m pytest test_dacs_%s.py --tb=short -s -v --junitxml=%s
EXIT_CODE=$?
cat %s 2>/dev/null || true
exit $EXIT_CODE
`, azureBlobPath, authEnv, testType, junitFile, junitFile)

	start := time.Now()
	output, err := pod.Exec(ctx, dacsPodName, c.namespace, testCmd)
	duration := time.Since(start)

	result := &TestResult{
		TestType: testType,
		Duration: duration,
		Output:   output,
		ExitCode: 0,
	}

	if err != nil {
		if strings.Contains(err.Error(), "exit code") {
			fmt.Sscanf(err.Error(), "command terminated with exit code %d", &result.ExitCode)
		} else {
			result.ExitCode = 1
		}
	}

	result.parseJUnitXML()

	log.Info("Python tests completed",
		"testType", testType,
		"passed", result.Passed,
		"failed", result.Failed,
		"skipped", result.Skipped,
		"duration", result.Duration,
		"exitCode", result.ExitCode,
	)

	if len(result.FailedTests) > 0 {
		log.Info("Failed tests", "tests", result.FailedTests)
	}

	// Collect pytest JUnit XML for later writing
	if c.pytestOutput != "" {
		junitXMLPath := fmt.Sprintf("/tmp/pytest_%s_results.xml", testType)
		tempFile := fmt.Sprintf("/tmp/pytest_%s_temp.xml", testType)

		// Copy to temp file first
		if copyErr := pod.CopyFrom(ctx, dacsPodName, c.namespace, junitXMLPath, tempFile); copyErr != nil {
			log.Error(copyErr, "Failed to copy pytest JUnit XML (non-fatal)")
		} else {
			// Add to collector for writing at the end
			if collectErr := collectPytestResults(tempFile); collectErr != nil {
				log.Error(collectErr, "Failed to collect pytest JUnit XML (non-fatal)")
			}
			os.Remove(tempFile)
		}
	}

	return result, nil
}

// copyTestFiles copies Python test files to the pod.
func (c *DACSAdapterTestConfig) copyTestFiles(ctx context.Context, testType TestType) error {
	By(fmt.Sprintf("Copying %s test files to pod", testType))

	projectDir, err := utils.GetProjectDir()
	if err != nil {
		return fmt.Errorf("failed to get project dir: %w", err)
	}

	dacsClientDir := filepath.Join(projectDir, "src", "subsystems", "storage", "dacs_client", "py_dacs_client")

	if err := pod.MkDir(ctx, dacsPodName, c.namespace, "/app/tests"); err != nil {
		return fmt.Errorf("failed to create test dir: %w", err)
	}

	// Copy test file: tests/test_dacs_<testType>.py
	testFile := filepath.Join(dacsClientDir, "tests", fmt.Sprintf("test_dacs_%s.py", testType))
	if err := pod.CopyTo(ctx, testFile, dacsPodName, c.namespace, "/app/tests/"); err != nil {
		return fmt.Errorf("failed to copy test file: %w", err)
	}

	// Copy conftest.py
	conftest := filepath.Join(dacsClientDir, "tests", "conftest.py")
	if _, err := os.Stat(conftest); err == nil {
		if err := pod.CopyTo(ctx, conftest, dacsPodName, c.namespace, "/app/tests/"); err != nil {
			log.Error(err, "Failed to copy conftest.py (non-fatal)")
		}
	}

	log.Info("Test files copied", "testType", testType)
	return nil
}

// parseJUnitXML parses JUnit XML from the output using go-junit-report library.
func (r *TestResult) parseJUnitXML() {
	xmlStart := strings.Index(r.Output, "<?xml")
	if xmlStart == -1 {
		return
	}

	xmlContent := r.Output[xmlStart:]
	var testsuites junit.Testsuites
	if err := xml.Unmarshal([]byte(xmlContent), &testsuites); err != nil {
		log.Error(err, "Failed to parse JUnit XML")
		return
	}

	for _, suite := range testsuites.Suites {
		for _, tc := range suite.Testcases {
			fullName := tc.Classname + "::" + tc.Name
			if tc.Classname == "" {
				fullName = tc.Name
			}

			switch {
			case tc.Failure != nil:
				r.Failed++
				r.FailedTests = append(r.FailedTests, fullName)
			case tc.Error != nil:
				r.Errors++
				r.FailedTests = append(r.FailedTests, fullName)
			case tc.Skipped != nil:
				r.Skipped++
			default:
				r.Passed++
				r.PassedTests = append(r.PassedTests, fullName)
			}
		}
	}
}

// collectPytestResults reads a pytest JUnit XML file and adds its testsuites to the collector.
func collectPytestResults(xmlFile string) error {
	data, err := os.ReadFile(xmlFile)
	if err != nil {
		return fmt.Errorf("failed to read JUnit XML: %w", err)
	}

	var testsuites junit.Testsuites
	if err := xml.Unmarshal(data, &testsuites); err != nil {
		return fmt.Errorf("failed to parse JUnit XML: %w", err)
	}

	for _, suite := range testsuites.Suites {
		pytestResultCollector.AddSuite(suite)
	}
	return nil
}

// WritePytestResults writes the collected pytest JUnit XML results to the specified file.
// This should be called once at the end of the test suite.
func WritePytestResults(outputPath string) error {
	if len(pytestResultCollector.Suites) == 0 {
		return nil
	}

	log.Info("Writing pytest JUnit XML", "path", outputPath, "suites", len(pytestResultCollector.Suites))

	outFile, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	defer outFile.Close()

	if _, err := outFile.WriteString(xml.Header); err != nil {
		return fmt.Errorf("failed to write XML header: %w", err)
	}
	return pytestResultCollector.WriteXML(outFile)
}
