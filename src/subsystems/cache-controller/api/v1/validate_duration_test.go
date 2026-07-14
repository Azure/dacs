/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package v1

import (
	"testing"
)

func TestValidateDuration(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{
			name:  "valid days only",
			input: "30d",
		},
		{
			name:  "valid hours only",
			input: "12h",
		},
		{
			name:  "valid minutes only",
			input: "45m",
		},
		{
			name:  "valid seconds only",
			input: "30s",
		},
		{
			name:  "valid days and hours",
			input: "7d12h",
		},
		{
			name:  "valid hours and minutes",
			input: "1h30m",
		},
		{
			name:  "valid full duration",
			input: "30d12h41m29s",
		},
		{
			name:  "valid days hours minutes",
			input: "1d2h3m",
		},
		{
			name:    "empty string",
			input:   "",
			wantErr: true,
		},
		{
			name:    "invalid format - no unit",
			input:   "30",
			wantErr: true,
		},
		{
			name:    "invalid format - wrong unit",
			input:   "30w",
			wantErr: true,
		},
		{
			name:    "invalid format - text",
			input:   "invalid",
			wantErr: true,
		},
		{
			name:    "invalid format - wrong order",
			input:   "30m1h",
			wantErr: true,
		},
		{
			name:    "invalid format - negative",
			input:   "-1d",
			wantErr: true,
		},
		{
			name:    "invalid format - decimal",
			input:   "1.5h",
			wantErr: true,
		},
		{
			name:    "invalid format - spaces",
			input:   "1d 2h",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := validateDuration(tt.input)
			if (err != nil) != tt.wantErr {
				t.Errorf("validateDuration(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}
