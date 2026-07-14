/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package termination

import (
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestWriteMessage(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name          string
		err           error
		msg           string
		keysAndValues []any
		want          string
	}{
		{
			name: "no context",
			err:  errors.New("something bad happened"),
			msg:  "unable to start manager",
			want: "unable to start manager: something bad happened",
		},
		{
			name:          "single pair",
			err:           errors.New("something bad happened"),
			msg:           "unable to start manager",
			keysAndValues: []any{"controller", "Cache"},
			want:          "unable to start manager: something bad happened (controller=Cache)",
		},
		{
			name:          "multiple pairs",
			err:           errors.New("something bad happened"),
			msg:           "unable to start manager",
			keysAndValues: []any{"controller", "Cache", "namespace", "default"},
			want:          "unable to start manager: something bad happened (controller=Cache, namespace=default)",
		},
		{
			name:          "non-string values",
			err:           errors.New("something bad happened"),
			msg:           "setup failed",
			keysAndValues: []any{"replicas", 3},
			want:          "setup failed: something bad happened (replicas=3)",
		},
		{
			name:          "dangling key",
			err:           errors.New("something bad happened"),
			msg:           "setup failed",
			keysAndValues: []any{"controller"},
			want:          "setup failed: something bad happened (controller=<missing>)",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			path := filepath.Join(t.TempDir(), "nested", "termination-log")
			if err := WriteMessage(path, tt.err, tt.msg, tt.keysAndValues...); err != nil {
				t.Fatalf("WriteMessage() returned unexpected error: %v", err)
			}
			got, err := os.ReadFile(path)
			if err != nil {
				t.Fatalf("reading termination message file: %v", err)
			}
			if string(got) != tt.want {
				t.Errorf("termination message = %q, want %q", string(got), tt.want)
			}
		})
	}
}
