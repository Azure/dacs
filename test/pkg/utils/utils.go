/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Utility methods to support e2e tests
# --------------------------------------------------------------------------------------------------------------------
*/
package utils

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/xml"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/logger"
)

var (
	// log is the default logger for the utils package. It uses klog's textlogger
	// to output in the standard klog format, writing to GinkgoWriter.
	log = logger.New()

	// WriteSupportModes defines the supported write modes for cache client e2e tests
	WriteSupportModes = map[string]bool{
		"write_cache_only":       true,
		"write_blob_only":        true,
		"write_cache_blob_async": true,
		"write_sync":             true,
	}
)

// NewStringReader creates a new io.Reader from a string.
func NewStringReader(s string) io.Reader {
	return strings.NewReader(s)
}

// runCmd executes a command using the provided output function.
func runCmd(cmd *exec.Cmd, getOutput func(*exec.Cmd) ([]byte, error)) (string, error) {
	dir, _ := GetProjectDir()
	cmd.Dir = dir
	if err := os.Chdir(cmd.Dir); err != nil {
		log.Error(err, "chdir failed")
	}

	cmd.Env = append(os.Environ(), "GO111MODULE=on")
	command := strings.Join(cmd.Args, " ")
	log.Info("running command", "command", command)
	output, err := getOutput(cmd)
	if err != nil {
		log.Error(err, "command failed", "command", command, "output", string(output))
	}
	return string(output), err
}

// Run executes the provided command and returns combined stdout/stderr.
func Run(cmd *exec.Cmd) (string, error) {
	return runCmd(cmd, (*exec.Cmd).CombinedOutput)
}

// RunOutput executes the provided command and returns stdout only.
func RunOutput(cmd *exec.Cmd) (string, error) {
	return runCmd(cmd, (*exec.Cmd).Output)
}

// GetNonEmptyLines converts given command output string into individual objects
// according to line breakers, and ignores the empty elements in it.
func GetNonEmptyLines(output string) []string {
	var res []string
	elements := strings.Split(output, "\n")
	for _, element := range elements {
		if element != "" {
			res = append(res, element)
		}
	}

	return res
}

// GetProjectDir will return the directory where the project is.
func GetProjectDir() (string, error) {
	wd, err := os.Getwd()
	if err != nil {
		return wd, err
	}
	wd = strings.ReplaceAll(wd, "/test/e2e/cacheClient", "")
	wd = strings.ReplaceAll(wd, "/test/e2e", "")
	wd = strings.ReplaceAll(wd, "/test/perf", "")

	return wd, nil
}

// RandomTag returns a random tag.
func RandomTag() string {
	bytes := make([]byte, 4)
	if _, err := rand.Read(bytes); err != nil {
		return ""
	}
	return hex.EncodeToString(bytes)
}

// UncommentCode searches for target in the file and remove the comment prefix
// of the target content. The target content may span multiple lines.
func UncommentCode(filename, target, prefix string) error {
	content, err := os.ReadFile(filename)
	if err != nil {
		return err
	}
	strContent := string(content)

	idx := strings.Index(strContent, target)
	if idx < 0 {
		return fmt.Errorf("unable to find the code %s to be uncomment", target)
	}

	out := new(bytes.Buffer)
	_, err = out.Write(content[:idx])
	if err != nil {
		return err
	}

	scanner := bufio.NewScanner(bytes.NewBufferString(target))
	if !scanner.Scan() {
		return nil
	}
	for {
		_, err := out.WriteString(strings.TrimPrefix(scanner.Text(), prefix))
		if err != nil {
			return err
		}
		// Avoid writing a newline in case the previous line was the last in target.
		if !scanner.Scan() {
			break
		}
		if _, err := out.WriteString("\n"); err != nil {
			return err
		}
	}

	_, err = out.Write(content[idx+len(target):])
	if err != nil {
		return err
	}
	return os.WriteFile(filename, out.Bytes(), 0644)
}

// FixupJunitReport removes fixes up the JUnit report file from Ginkgo for Junit
// - Removes skipped/pending test cases
// - Replaces brackets with parentheses because Junit is not handling brackets properly.
func FixupJunitReport(xmlReportFile string) error {
	data, err := os.ReadFile(xmlReportFile)
	if err != nil {
		return err
	}

	decoder := xml.NewDecoder(bytes.NewReader(data))
	var buf bytes.Buffer
	encoder := xml.NewEncoder(&buf)

	for {
		tok, err := decoder.Token()
		if err != nil {
			if err.Error() == "EOF" {
				break
			}
			return err
		}

		switch t := tok.(type) {
		case xml.StartElement:
			if t.Name.Local == "testcase" {
				var status string
				var newAttrs []xml.Attr
				for _, attr := range t.Attr {
					if attr.Name.Local == "status" {
						status = attr.Value
					}
					if attr.Name.Local == "classname" {
						continue
					}
					if attr.Name.Local == "name" {
						attr.Value = strings.ReplaceAll(strings.ReplaceAll(attr.Value, "[", "("), "]", ")")
					}
					newAttrs = append(newAttrs, attr)
				}
				if status == "skipped" || status == "pending" {
					// Skip this testcase
					for {
						tok, err = decoder.Token()
						if err != nil || tok == nil {
							break
						}
						if end, ok := tok.(xml.EndElement); ok && end.Name.Local == "testcase" {
							break
						}
					}
					continue
				}
				t.Attr = newAttrs
				tok = t
			}
		}
		if err := encoder.EncodeToken(tok); err != nil {
			return fmt.Errorf("failed to encode token: %w", err)
		}
	}

	if err := encoder.Flush(); err != nil {
		return fmt.Errorf("failed to flush encoder: %w", err)
	}
	return os.WriteFile(xmlReportFile, buf.Bytes(), os.ModePerm)
}

// CollectSupportBundle runs the make get-support-bundle command with the
// SUPPORT_BUNDLE_SINCE_TIME environment variable set.
func CollectSupportBundle(ctx context.Context, outputFile string, startTime time.Time) (string, error) {
	// Format the startTime to the required format
	sinceTime := startTime.Format(time.RFC3339)

	// Get project directory to run make from
	dir, err := GetProjectDir()
	if err != nil {
		return "", fmt.Errorf("failed to get project directory: %w", err)
	}

	// Set the SUPPORT_BUNDLE_SINCE_TIME environment variable
	cmd := exec.CommandContext(ctx, "make", "get-support-bundle")
	cmd.Dir = dir
	cmd.Env = append(os.Environ(), "SUPPORT_BUNDLE_SINCE_TIME="+sinceTime, "SUPPORT_BUNDLE_OUTPUT="+outputFile)

	// Run the command
	output, err := cmd.CombinedOutput()
	if err != nil {
		if exitErr, ok := err.(*exec.ExitError); ok && exitErr != nil {
			// If the command failed, return stderr.
			output = exitErr.Stderr
		}
		return string(output), err
	}

	return string(output), nil
}

// Sleep sleeps for the given duration or until the context is done.
// Useful for our tests where we might want to be able to cancel the
// test during the sleep.
func Sleep(ctx context.Context, delay time.Duration) error {
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(delay):
		return nil
	}
}

// TODO: Move the following functions to a separate azure utils package
// CheckRoleAssignments checks if a role assignment exists for the given clientID on the storage account
func CheckRoleAssignments(clientID, storageAccountName, role string) (bool, error) {
	scope, err := getSAScope(storageAccountName)
	if err != nil {
		return false, err
	}
	objectID, err := getObjectID(clientID)
	if err != nil {
		return false, err
	}
	return isRoleAssignmentAlreadyExists(objectID, scope, role)
}

// getSAScope queryes the scope for the given storage account
func getSAScope(sa string) (string, error) {
	cmd := exec.Command("az", "storage", "account", "show", "--name", sa, "--query", "id", "-o", "tsv")
	output, err := Run(cmd)
	if err != nil {
		return "", fmt.Errorf("Failed to get storage account id for %s: %v", sa, err)
	}
	return strings.TrimSpace(string(output)), nil
}

// getObjectID returns the principal ID for the given clientID
func getObjectID(clientID string) (string, error) {
	cmd := exec.Command("az", "ad", "sp", "show", "--id", clientID, "--query", "id", "-o", "tsv")
	output, err := Run(cmd)
	if err != nil {
		return "", fmt.Errorf("Failed to get principal id for clientID %s: %v", clientID, err)
	}
	return strings.TrimSpace(string(output)), nil
}

func isRoleAssignmentAlreadyExists(objectID, scope, role string) (bool, error) {
	cmd := exec.Command("az", "role", "assignment", "list", "--assignee", objectID, "--scope", scope, "--role", role, "--query", "[].id", "-o", "tsv")
	output, err := Run(cmd)
	if err != nil {
		return false, fmt.Errorf("Failed to check role assignments for objectID %s: %v", objectID, err)
	}
	if strings.TrimSpace(string(output)) != "" {
		return true, nil
	}
	return false, nil
}

// CreateContainerIfNotExists checks and create the container if not exists
func CreateContainerIfNotExists(storageAccountName, containerName string) error {
	// Check container existence
	cmd := exec.Command("az", "storage", "container", "exists", "--name", containerName, "--account-name", storageAccountName, "--query", "exists", "-o", "tsv")
	output, err := Run(cmd)
	if err != nil {
		return fmt.Errorf("Failed to check container existence: %v", err)
	}
	if strings.TrimSpace(string(output)) == "true" {
		return nil
	}

	// Create container
	cmd = exec.Command("az", "storage", "container", "create", "--name", containerName, "--account-name", storageAccountName)
	_, err = Run(cmd)
	if err != nil {
		return fmt.Errorf("Failed to create container: %v", err)
	}
	return nil
}

// DeleteContainer marks the specified container for deletion
func DeleteContainer(storageAccountName, containerName string) error {
	cmd := exec.Command("az", "storage", "container", "delete", "--name", containerName, "--account-name", storageAccountName)
	_, err := Run(cmd)
	if err != nil {
		return fmt.Errorf("Failed to delete container: %v", err)
	}
	return nil
}

// GetResourceGroupOfCluster returns the resource group of the given AKS cluster name
func GetResourceGroupOfCluster(clusterName string) (string, error) {
	cmd := exec.Command("az", "aks", "list", "--query", fmt.Sprintf("[?name=='%s'].resourceGroup", clusterName), "-o", "tsv")
	output, err := RunOutput(cmd)
	if err != nil {
		return "", fmt.Errorf("Failed to get resource group of cluster %s: %v", clusterName, err)
	}
	return strings.TrimSpace(string(output)), nil
}

// GetRoleAssignmentCommand returns the Azure CLI command to create role assignment
// for the storage account using Bicep template.
func GetRoleAssignmentCommand(saName, clusterName, resourceGroup string) string {
	return "az deployment group create --resource-group " + resourceGroup + " " +
		"--template-file deploy/template/storage.bicep " +
		"--parameters storageAccountName=" + saName + " " +
		"aksName=" + clusterName + " aksResourceGroup=" + resourceGroup
}
