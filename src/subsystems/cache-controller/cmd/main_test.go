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

package main

import (
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"sigs.k8s.io/controller-runtime/pkg/healthz"

	"go.uber.org/zap/zapcore"
	"gotest.tools/v3/assert"
)

func TestGetZapLogLevelInfo(t *testing.T) {
	level := getZapLogLevel("info")
	assert.Equal(t, zapcore.InfoLevel, level)
}

func TestGetZapLogLevelDebug(t *testing.T) {
	level := getZapLogLevel("debug")
	assert.Equal(t, zapcore.DebugLevel, level)
}

func TestGetZapLogLevelError(t *testing.T) {
	level := getZapLogLevel("error")
	assert.Equal(t, zapcore.ErrorLevel, level)
}

func TestGetZapLogLevelWarn(t *testing.T) {
	level := getZapLogLevel("warn")
	assert.Equal(t, zapcore.WarnLevel, level)
}

func TestGetZapLogLevelInvalid(t *testing.T) {
	// Invalid log level should default to info
	level := getZapLogLevel("invalid-level")
	assert.Equal(t, zapcore.InfoLevel, level)
}

func TestGetZapLogLevelEmpty(t *testing.T) {
	// Empty log level should default to info
	level := getZapLogLevel("")
	assert.Equal(t, zapcore.InfoLevel, level)
}

func TestRemoveUnusedNodeFields(t *testing.T) {
	in := &corev1.Node{
		ObjectMeta: metav1.ObjectMeta{
			Name:          "node-1",
			Labels:        map[string]string{"role": "worker"},
			ManagedFields: []metav1.ManagedFieldsEntry{{Manager: "kubelet"}},
		},
		Spec: corev1.NodeSpec{Unschedulable: true},
		Status: corev1.NodeStatus{
			Allocatable: corev1.ResourceList{
				corev1.ResourceMemory: resource.MustParse("8Gi"),
			},
			Capacity: corev1.ResourceList{
				corev1.ResourceMemory: resource.MustParse("16Gi"),
			},
			Conditions: []corev1.NodeCondition{{Type: corev1.NodeReady}},
			Images:     []corev1.ContainerImage{{Names: []string{"nginx:latest"}}},
		},
	}

	out, err := removeUnusedNodeFields(in)
	assert.NilError(t, err)
	got, ok := out.(*corev1.Node)
	assert.Assert(t, ok, "expected *corev1.Node, got %T", out)

	// Removed fields must be nil.
	assert.Assert(t, got.ManagedFields == nil, "ManagedFields not removed")
	assert.Assert(t, got.Status.Conditions == nil, "Status.Conditions not removed")
	assert.Assert(t, got.Status.Images == nil, "Status.Images not removed")

	// Preserved fields must be intact (these are read by the controller).
	assert.Equal(t, "node-1", got.Name)
	assert.Equal(t, "worker", got.Labels["role"])
	assert.Assert(t, got.Spec.Unschedulable)
	assert.Equal(t, "8Gi", got.Status.Allocatable.Memory().String())
	assert.Equal(t, "16Gi", got.Status.Capacity.Memory().String())
}

func TestRemoveUnusedNodeFields_NonNodeInput(t *testing.T) {
	cm := &corev1.ConfigMap{ObjectMeta: metav1.ObjectMeta{Name: "x"}}
	out, err := removeUnusedNodeFields(cm)
	assert.NilError(t, err)
	assert.Equal(t, any(cm), out, "expected pass-through for non-Node input")
}

type healthCheckRecorder struct {
	checks map[string]healthz.Checker
}

func (r *healthCheckRecorder) AddHealthzCheck(name string, checker healthz.Checker) error {
	r.checks[name] = checker
	return nil
}

func (r *healthCheckRecorder) AddReadyzCheck(name string, checker healthz.Checker) error {
	r.checks[name] = checker
	return nil
}

func TestSetupHealthChecks(t *testing.T) {
	tests := []struct {
		name           string
		enableWebhooks bool
		certsReady     bool
		webhookErr     error
		wantErr        string
	}{
		{name: "webhooks disabled"},
		{name: "certificates pending", enableWebhooks: true, wantErr: "certs are not ready yet"},
		{
			name:           "webhook starting",
			enableWebhooks: true,
			certsReady:     true,
			webhookErr:     errors.New("webhook server has not started"),
			wantErr:        "webhook server has not started",
		},
		{name: "webhook ready", enableWebhooks: true, certsReady: true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			certsReady := make(chan struct{})
			if tt.certsReady {
				close(certsReady)
			}
			recorder := &healthCheckRecorder{checks: make(map[string]healthz.Checker)}
			webhookCalls := 0
			err := setupHealthChecks(recorder, tt.enableWebhooks, certsReady, func(*http.Request) error {
				webhookCalls++
				return tt.webhookErr
			})
			assert.NilError(t, err)
			assert.Assert(t, recorder.checks["healthz"] != nil, "liveness check must be registered")
			assert.Assert(t, recorder.checks["readyz"] != nil, "readiness check must be registered")
			req := httptest.NewRequestWithContext(t.Context(), http.MethodGet, "/healthz", nil)
			assert.NilError(t, recorder.checks["healthz"](req), "liveness must not depend on certificate or webhook readiness")
			req = httptest.NewRequestWithContext(t.Context(), http.MethodGet, "/readyz", nil)
			err = recorder.checks["readyz"](req)
			if tt.wantErr == "" {
				assert.NilError(t, err)
			} else {
				assert.Error(t, err, tt.wantErr)
			}
			wantWebhookCalls := 0
			if tt.enableWebhooks && tt.certsReady {
				wantWebhookCalls = 1
			}
			assert.Equal(t, webhookCalls, wantWebhookCalls, "only check webhook startup after certificates are ready")
		})
	}
}
