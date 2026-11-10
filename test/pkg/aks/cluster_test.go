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

package aks

import "testing"

func TestSelectNodePool(t *testing.T) {
	for _, tt := range []struct {
		name     string
		pools    []NodePool
		override string
		want     NodePool
		wantErr  bool
	}{
		{name: "single pool", pools: []NodePool{{"system", 6}}, want: NodePool{"system", 6}},
		{name: "largest pool first", pools: []NodePool{{"user", 6}, {"system", 2}}, want: NodePool{"user", 6}},
		{name: "largest pool last", pools: []NodePool{{"system", 2}, {"user", 6}}, want: NodePool{"user", 6}},
		{name: "tie selects first", pools: []NodePool{{"first", 2}, {"second", 2}}, want: NodePool{"first", 2}},
		{name: "override", pools: []NodePool{{"system", 6}, {"user", 2}}, override: "user", want: NodePool{"user", 2}},
		{name: "unknown override", pools: []NodePool{{"system", 6}}, override: "missing", wantErr: true},
	} {
		t.Run(tt.name, func(t *testing.T) {
			got, err := selectNodePool(tt.pools, tt.override)
			if (err != nil) != tt.wantErr || got != tt.want {
				t.Fatalf("selectNodePool() = %+v, %v; want %+v, error=%t", got, err, tt.want, tt.wantErr)
			}
		})
	}
}
