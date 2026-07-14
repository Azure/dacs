/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

// Package termination builds and writes the Kubernetes termination message
// that surfaces a container's last error as its termination reason.
package termination

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// DefaultMessagePath is the path Kubernetes reads a container's termination
// reason from. The kubelet surfaces the file's contents as the container's
// termination message.
const DefaultMessagePath = "/dev/termination-log"

// formatMessage builds the termination message string from an error, the
// contextual message describing what failed, and optional structured
// key/value pairs. The key/value pairs are appended in parentheses, mirroring
// the structured logging context (e.g. "msg: err (key1=val1, key2=val2)").
// A key without a corresponding value is rendered with a "<missing>" value.
func formatMessage(err error, msg string, keysAndValues ...any) string {
	errMsg := fmt.Sprintf("%s: %v", msg, err)
	if len(keysAndValues) == 0 {
		return errMsg
	}

	var sb strings.Builder
	for i := 0; i < len(keysAndValues); i += 2 {
		if i > 0 {
			sb.WriteString(", ")
		}
		key := fmt.Sprintf("%v", keysAndValues[i])
		val := "<missing>"
		if i+1 < len(keysAndValues) {
			val = fmt.Sprintf("%v", keysAndValues[i+1])
		}
		_, _ = fmt.Fprintf(&sb, "%s=%s", key, val)
	}

	return fmt.Sprintf("%s (%s)", errMsg, sb.String())
}

// WriteMessage writes the formatted termination message to path, creating any
// missing parent directories. It returns a wrapped error identifying which step
// failed so callers can log a meaningful reason.
func WriteMessage(path string, err error, msg string, keysAndValues ...any) error {
	content := formatMessage(err, msg, keysAndValues...)

	parentDir := filepath.Dir(path)
	if mkErr := os.MkdirAll(parentDir, 0755); mkErr != nil {
		return fmt.Errorf("failed to create directory for termination message: %w", mkErr)
	}

	if wErr := os.WriteFile(path, []byte(content), 0600); wErr != nil {
		return fmt.Errorf("failed to write termination message: %w", wErr)
	}

	return nil
}
