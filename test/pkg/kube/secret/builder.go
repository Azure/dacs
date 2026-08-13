/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Kubernetes Secret builder helper methods
# --------------------------------------------------------------------------------------------------------------------
*/
package secret

import (
	corev1 "k8s.io/api/core/v1"
)

type Secret struct {
	*corev1.Secret
}

func NewSecret() *Secret {
	return &Secret{&corev1.Secret{}}
}

// WithName sets the Secret name.
func (s *Secret) WithName(name string) *Secret {
	s.Name = name
	return s
}

// WithNamespace sets the Secret namespace.
func (s *Secret) WithNamespace(namespace string) *Secret {
	s.Namespace = namespace
	return s
}

// WithData replaces the Secret Data map with the provided map.
// Note: values are expected as raw bytes.
func (s *Secret) WithData(data map[string]string) *Secret {
	s.Data = make(map[string][]byte, len(data))
	for k, v := range data {
		// copy slice to avoid accidental mutation by caller
		if v == "" {
			s.Data[k] = nil
			continue
		}
		s.Data[k] = []byte(v)
	}
	return s
}

// AddData adds or updates a single key/value in the Secret Data map.
func (s *Secret) AddData(key string, value []byte) *Secret {
	if s.Data == nil {
		s.Data = make(map[string][]byte)
	}
	if value == nil {
		s.Data[key] = nil
		return s
	}
	b := make([]byte, len(value))
	copy(b, value)
	s.Data[key] = b
	return s
}
