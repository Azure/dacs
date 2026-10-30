// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package featuregate

import (
	"fmt"
	"strings"
)

// Gate names are defined here as constants.
const (
	// UseZeroCopy enables zero-copy data transfers in the cache server.
	UseZeroCopy = "UseZeroCopy"

	// UseFlatRadixLocatorIndex runs the cache server's locator on the radix index
	// (CACHESERVER_LOCATOR_INDEX=flatradix) instead of the default flat map. The
	// radix index answers chain prefix lookups, which the flat map cannot.
	UseFlatRadixLocatorIndex = "UseFlatRadixLocatorIndex"
)

// defaultGates defines the default state of each known feature gate.
var defaultGates = map[string]bool{
	UseZeroCopy:              false,
	UseFlatRadixLocatorIndex: false,
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
