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

package config

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/spf13/viper"
)

func TestImagePullSecrets(t *testing.T) {
	for _, tc := range []struct {
		name, file, env string
		want            []string
		invalid         bool
	}{
		{name: "default"},
		{name: "file", file: "imagePullSecrets: [first, second]\n", want: []string{"first", "second"}},
		{name: "environment", env: "china-acr,harbor", want: []string{"china-acr", "harbor"}},
		{name: "environment overrides file", file: "imagePullSecrets: [old]\n", env: "new", want: []string{"new"}},
		{name: "invalid name", env: "not/a/secret", invalid: true},
		{name: "invalid file name", file: "imagePullSecrets: [bad..name]\n", invalid: true},
		{name: "empty file name", file: "imagePullSecrets: ['']\n", invalid: true},
		{name: "empty environment entry", env: "first,,last", invalid: true},
		{name: "long name", env: strings.Repeat("a", 254), invalid: true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			viper.Reset()
			t.Cleanup(viper.Reset)
			t.Setenv("CACHE_CONTROLLER_IMAGEPULLSECRETS", tc.env)
			file := filepath.Join(t.TempDir(), "config.yaml")
			if err := os.WriteFile(file, []byte("cacheServerImage: cache-server:local\n"+tc.file), 0600); err != nil {
				t.Fatal(err)
			}
			cfg, err := loadConfigInternal(file)
			if tc.invalid {
				if err == nil || !strings.Contains(err.Error(), "invalid imagePullSecrets name") {
					t.Fatalf("expected clear invalid secret name error, got %v", err)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			if len(cfg.ImagePullSecrets) != len(tc.want) || (len(tc.want) > 0 && !reflect.DeepEqual(cfg.ImagePullSecrets, tc.want)) {
				t.Fatalf("imagePullSecrets = %v, want %v", cfg.ImagePullSecrets, tc.want)
			}
		})
	}
}
