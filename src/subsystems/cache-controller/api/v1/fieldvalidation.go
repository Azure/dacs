/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"time"

	"k8s.io/apimachinery/pkg/util/validation"
)

// The controller renders several Cache-controlled string fields into a Go
// text/template that produces Kubernetes YAML and a shell program. Without
// validation, a crafted value can close its expected token and inject
// additional structure (extra PodSpec fields, resource keys, or shell tokens).
// These validators reject values that are not legal for the position in which
// they are used, so no accepted value can alter the structure of a generated
// workload.
//
// They are shared by the admission webhook and the reconciler so the controller
// fails closed even when admission is disabled or bypassed. They reject invalid
// input; they never silently rewrite it.

// pathAllowedChars matches a single character that is safe inside a host path.
// It intentionally excludes whitespace, YAML structure characters, and shell
// metacharacters.
const pathAllowedChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/"

// durationRegex matches the composite duration syntax accepted by the cache
// server, for example "30d", "1h30m", or "30d12h41m29s". It admits only digits
// and the unit suffixes, so an accepted value can never carry a quote, newline,
// or other character that would alter the structure of generated YAML.
var durationRegex = regexp.MustCompile(`^(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$`)

// maxDurationComponent bounds a single duration component so that accumulating
// the total cannot overflow time.Duration.
const maxDurationComponent = 1_000_000

// MaxBucketInterval is the largest access-time bucket width the cache server
// honours; larger values are ignored in favour of the server default, so they
// are rejected at admission instead of silently taking effect.
const MaxBucketInterval = 7 * 24 * time.Hour

// parseDuration converts the composite duration syntax into a time.Duration.
func parseDuration(s string) (time.Duration, error) {
	if s == "" {
		return 0, fmt.Errorf("must not be empty")
	}
	groups := durationRegex.FindStringSubmatch(s)
	if groups == nil {
		return 0, fmt.Errorf("must match pattern like '30d', '1h30m', '30d12h41m29s'")
	}
	units := []time.Duration{24 * time.Hour, time.Hour, time.Minute, time.Second}
	var total time.Duration
	for i, unit := range units {
		component := groups[i+1]
		if component == "" {
			continue
		}
		value, err := strconv.ParseUint(component, 10, 64)
		if err != nil || value > maxDurationComponent {
			return 0, fmt.Errorf("component %q is too large", component)
		}
		total += time.Duration(value) * unit
	}
	return total, nil
}

// ValidateDuration rejects any value that is not a well-formed composite
// duration. Duration-typed Cache fields are rendered into container environment
// variables, so they must not be free-form.
func ValidateDuration(value string) error {
	if _, err := parseDuration(value); err != nil {
		return err
	}
	return nil
}

// ValidateBucketInterval rejects any eviction bucket interval that is not a
// well-formed duration, is not positive, or exceeds the maximum the cache
// server accepts.
func ValidateBucketInterval(value string) error {
	interval, err := parseDuration(value)
	if err != nil {
		return err
	}
	if interval <= 0 {
		return fmt.Errorf("must be greater than zero")
	}
	if interval > MaxBucketInterval {
		return fmt.Errorf("must be at most 1 week (%s), got %q", MaxBucketInterval, value)
	}
	return nil
}

// ValidatePercentage rejects any value that is not a plain integer percentage
// in the range 0-100. Percentage fields are rendered into container environment
// variables, so they must not be free-form.
func ValidatePercentage(value string, fieldName string) error {
	if strings.TrimSpace(value) != value {
		return fmt.Errorf("%s must not contain leading or trailing whitespace", fieldName)
	}
	parsed, err := strconv.Atoi(value)
	if err != nil {
		return fmt.Errorf("%s must be a numeric string representing a percentage (0-100)", fieldName)
	}
	if parsed < 0 || parsed > 100 {
		return fmt.Errorf("%s must be between 0 and 100, got %d", fieldName, parsed)
	}
	return nil
}

// ValidateNodeSelectorKey rejects any value that is not a valid Kubernetes label
// key. The value is rendered as a node affinity match-expression key.
func ValidateNodeSelectorKey(key string) error {
	if msgs := validation.IsQualifiedName(key); len(msgs) > 0 {
		return fmt.Errorf("must be a valid label key: %s", strings.Join(msgs, "; "))
	}
	return nil
}

// ValidateNodeSelectorValue rejects any value that is not a valid Kubernetes
// label value. The value is rendered into a node affinity match-expression
// values list.
func ValidateNodeSelectorValue(value string) error {
	if msgs := validation.IsValidLabelValue(value); len(msgs) > 0 {
		return fmt.Errorf("must be a valid label value: %s", strings.Join(msgs, "; "))
	}
	return nil
}

// ValidateInfinibandResourceName rejects any value that is not a valid
// Kubernetes qualified resource name. The value is rendered as a map key under
// both resource requests and limits.
func ValidateInfinibandResourceName(name string) error {
	if msgs := validation.IsQualifiedName(name); len(msgs) > 0 {
		return fmt.Errorf("must be a valid resource name: %s", strings.Join(msgs, "; "))
	}
	return nil
}

// ValidateNodeCacheFolderPath rejects host paths that are not absolute, that
// contain characters unsafe for YAML or shell contexts, or that contain a
// parent-directory ("..") segment.
func ValidateNodeCacheFolderPath(path string) error {
	if path == "" {
		return fmt.Errorf("must not be empty")
	}
	if !strings.HasPrefix(path, "/") {
		return fmt.Errorf("must be an absolute path")
	}
	for _, r := range path {
		if !strings.ContainsRune(pathAllowedChars, r) {
			return fmt.Errorf("must contain only letters, digits, and the characters '.', '-', '_', '/'; found %q", r)
		}
	}
	for _, seg := range strings.Split(path, "/") {
		if seg == ".." {
			return fmt.Errorf("must not contain a '..' path segment")
		}
	}
	return nil
}
