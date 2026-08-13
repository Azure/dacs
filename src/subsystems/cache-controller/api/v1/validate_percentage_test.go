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

package v1

import (
	"testing"
)

func TestValidatePercentage(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{
			name:  "valid zero",
			input: "0",
		},
		{
			name:  "valid mid-range",
			input: "50",
		},
		{
			name:  "valid max",
			input: "100",
		},
		{
			name:  "valid single digit",
			input: "5",
		},
		{
			name:    "invalid negative",
			input:   "-1",
			wantErr: true,
		},
		{
			name:    "invalid over 100",
			input:   "101",
			wantErr: true,
		},
		{
			name:    "invalid way over 100",
			input:   "150",
			wantErr: true,
		},
		{
			name:    "invalid non-numeric",
			input:   "abc",
			wantErr: true,
		},
		{
			name:    "invalid empty",
			input:   "",
			wantErr: true,
		},
		{
			name:    "invalid decimal",
			input:   "50.5",
			wantErr: true,
		},
		{
			name:    "invalid with percent sign",
			input:   "50%",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := validatePercentage(tt.input, "testField")
			if (err != nil) != tt.wantErr {
				t.Errorf("validatePercentage(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}
