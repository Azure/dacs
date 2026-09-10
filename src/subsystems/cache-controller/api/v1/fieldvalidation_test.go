/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

import "testing"

func TestValidateNodeSelectorKey(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{name: "default key", input: "dacs.azure.com/processing-unit"},
		{name: "simple key", input: "kubernetes.io/hostname"},
		{name: "bare name", input: "gpu"},
		{name: "newline injection", input: "gpu\n      nodeName: victim", wantErr: true},
		{name: "space", input: "a b", wantErr: true},
		{name: "yaml close", input: "k}\n", wantErr: true},
		{name: "empty", input: "", wantErr: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := ValidateNodeSelectorKey(tt.input)
			if (err != nil) != tt.wantErr {
				t.Fatalf("ValidateNodeSelectorKey(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}

func TestValidateNodeSelectorValue(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{name: "default value", input: "gpu"},
		{name: "empty is a valid label value", input: ""},
		{name: "alnum with dash", input: "gpu-a100"},
		{name: "yaml sequence break", input: "gpu]\n      nodeName: victim-node", wantErr: true},
		{name: "newline", input: "gpu\nx", wantErr: true},
		{name: "bracket", input: "gpu]", wantErr: true},
		{name: "brace", input: "gpu}", wantErr: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := ValidateNodeSelectorValue(tt.input)
			if (err != nil) != tt.wantErr {
				t.Fatalf("ValidateNodeSelectorValue(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}

func TestValidateInfinibandResourceName(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{name: "default", input: "rdma/hca_rdma_infiniband"},
		{name: "simple", input: "nvidia.com/gpu"},
		{name: "yaml key injection", input: "rdma/x: 1\n            evil", wantErr: true},
		{name: "newline", input: "rdma/x\ny", wantErr: true},
		{name: "space", input: "rdma x", wantErr: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := ValidateInfinibandResourceName(tt.input)
			if (err != nil) != tt.wantErr {
				t.Fatalf("ValidateInfinibandResourceName(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}

func TestValidateNodeCacheFolderPath(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		input   string
		wantErr bool
	}{
		{name: "default path", input: "/var/lib/ssd/cacheserver"},
		{name: "nested", input: "/var/lib/ssd/cacheserver/sub-dir_1"},
		{name: "root", input: "/"},
		{name: "empty", input: "", wantErr: true},
		{name: "relative", input: "var/lib/ssd", wantErr: true},
		{name: "newline injection", input: "/var/lib\n      nodeName: victim", wantErr: true},
		{name: "shell metachar dollar", input: "/var/lib/$IFS", wantErr: true},
		{name: "shell command sub", input: "/var/lib/$(id)", wantErr: true},
		{name: "space", input: "/var/lib/a b", wantErr: true},
		{name: "quote", input: "/var/lib/\"x", wantErr: true},
		{name: "parent traversal", input: "/var/lib/../../etc", wantErr: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			err := ValidateNodeCacheFolderPath(tt.input)
			if (err != nil) != tt.wantErr {
				t.Fatalf("ValidateNodeCacheFolderPath(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			}
		})
	}
}

func TestValidateBucketInterval(t *testing.T) {
	valid := []string{"15m", "1h", "60s", "7d", "6d23h59m59s"}
	for _, v := range valid {
		if err := ValidateBucketInterval(v); err != nil {
			t.Errorf("expected %q to be accepted, got %v", v, err)
		}
	}

	invalid := []string{
		"",
		"0s",
		"8d",
		"1w",
		"15",
		"15 m",
		"15m\"\n        - name: LD_PRELOAD\n          value: \"/evil.so",
		"15m; rm -rf /",
		"9999999999d",
	}
	for _, v := range invalid {
		if err := ValidateBucketInterval(v); err == nil {
			t.Errorf("expected %q to be rejected, got nil", v)
		}
	}
}
