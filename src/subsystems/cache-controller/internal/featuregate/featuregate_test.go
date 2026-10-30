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

import "testing"

// resetGates restores every gate to its default when the test ends.
func resetGates(t *testing.T) {
	t.Helper()
	t.Cleanup(func() {
		for name, value := range defaultGates {
			gates[name] = value
		}
	})
}

func TestGatesAreOffByDefault(t *testing.T) {
	for _, name := range []string{UseZeroCopy, UseFlatRadixLocatorIndex} {
		if Enabled(name) {
			t.Errorf("gate %s is enabled by default", name)
		}
	}
}

func TestParseEnablesAndDisablesGates(t *testing.T) {
	resetGates(t)
	if err := Parse("UseFlatRadixLocatorIndex=true, UseZeroCopy=false"); err != nil {
		t.Fatalf("Parse returned an error: %v", err)
	}
	if !Enabled(UseFlatRadixLocatorIndex) {
		t.Error("UseFlatRadixLocatorIndex not enabled")
	}
	if Enabled(UseZeroCopy) {
		t.Error("UseZeroCopy enabled")
	}
	if err := Parse("UseFlatRadixLocatorIndex=false"); err != nil {
		t.Fatalf("Parse returned an error: %v", err)
	}
	if Enabled(UseFlatRadixLocatorIndex) {
		t.Error("UseFlatRadixLocatorIndex still enabled")
	}
}

func TestParseRejectsInvalidInput(t *testing.T) {
	resetGates(t)
	for _, raw := range []string{
		"UnknownGate=true",
		"UseFlatRadixLocatorIndex",
		"UseFlatRadixLocatorIndex=yes",
	} {
		if err := Parse(raw); err == nil {
			t.Errorf("Parse(%q) returned no error", raw)
		}
	}
}
