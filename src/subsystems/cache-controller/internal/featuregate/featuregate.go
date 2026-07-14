/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package featuregate

import (
	"fmt"
	"strings"
)

// Gate names — add new feature gates here as constants.
const (
	// UseZeroCopy enables zero-copy data transfers in the cache server.
	UseZeroCopy = "UseZeroCopy"
)

// defaultGates defines the default state of each known feature gate.
var defaultGates = map[string]bool{
	UseZeroCopy: false,
}

var gates map[string]bool

func init() {
	gates = make(map[string]bool, len(defaultGates))
	for k, v := range defaultGates {
		gates[k] = v
	}
}

// Parse parses a comma-separated list of key=value pairs (e.g.
// "UseZeroCopy=true,Foo=false") and updates the gate states.
// Unknown gate names return an error.
func Parse(raw string) error {
	if raw == "" {
		return nil
	}

	for _, item := range strings.Split(raw, ",") {
		parts := strings.SplitN(strings.TrimSpace(item), "=", 2)
		if len(parts) != 2 {
			return fmt.Errorf("invalid feature gate format %q, expected key=value", item)
		}
		key := strings.TrimSpace(parts[0])
		val := strings.TrimSpace(parts[1])

		if _, known := defaultGates[key]; !known {
			return fmt.Errorf("unknown feature gate %q", key)
		}

		switch val {
		case "true":
			gates[key] = true
		case "false":
			gates[key] = false
		default:
			return fmt.Errorf("invalid value %q for feature gate %q, expected true or false", val, key)
		}
	}
	return nil
}

// Enabled returns whether the named feature gate is enabled.
func Enabled(name string) bool {
	return gates[name]
}
