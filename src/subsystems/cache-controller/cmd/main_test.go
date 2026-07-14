/*
Copyright (c) Microsoft Corporation.
Licensed under the Apache License, Version 2.0.
*/

package main

import (
	"testing"

	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"

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
