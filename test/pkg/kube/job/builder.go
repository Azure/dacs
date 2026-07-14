/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# Kubernetes Job builder helper methods
# --------------------------------------------------------------------------------------------------------------------
*/

package job

import (
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

type JobConfig struct {
	*batchv1.Job
}

func NewJobConfig(job *batchv1.Job) *JobConfig {
	return &JobConfig{Job: job}
}

func (jc *JobConfig) WithName(name string) *JobConfig {
	jc.Name = name
	return jc
}

func (jc *JobConfig) WithParallelism(parallelism int32) *JobConfig {
	jc.Spec.Parallelism = &parallelism
	return jc
}

func (jc *JobConfig) WithCompletions(completions int32) *JobConfig {
	jc.Spec.Completions = &completions
	return jc
}

func (jc *JobConfig) WithNamespace(namespace string) *JobConfig {
	jc.Namespace = namespace
	return jc
}

func (jc *JobConfig) WithMatchLabels(labels map[string]string) *JobConfig {
	if jc.Labels == nil {
		jc.Labels = make(map[string]string)
	}
	for key, value := range labels {
		jc.Labels[key] = value
	}
	jc.Labels["app"] = jc.Name
	jc.Spec.Template.Labels = jc.Labels
	return jc
}

func (jc *JobConfig) WithPodAntiAffinity() *JobConfig {
	if jc.Spec.Parallelism != nil && *jc.Spec.Parallelism > 1 {
		if jc.Spec.Template.Spec.Affinity == nil {
			jc.Spec.Template.Spec.Affinity = &corev1.Affinity{}
		}
		if jc.Spec.Template.Spec.Affinity.PodAntiAffinity == nil {
			jc.Spec.Template.Spec.Affinity.PodAntiAffinity = &corev1.PodAntiAffinity{}
		}

		isExpressionUpdated := false
		for i := range jc.Spec.Template.Spec.Affinity.PodAntiAffinity.RequiredDuringSchedulingIgnoredDuringExecution {
			affinityTerm := &jc.Spec.Template.Spec.Affinity.PodAntiAffinity.RequiredDuringSchedulingIgnoredDuringExecution[i]
			for j := range affinityTerm.LabelSelector.MatchExpressions {
				expr := &affinityTerm.LabelSelector.MatchExpressions[j]
				if expr.Key == "app" {
					expr.Values = []string{jc.Name}
					isExpressionUpdated = true
				}
			}
		}
		if !isExpressionUpdated {
			antiAffinityTerm := corev1.PodAffinityTerm{
				LabelSelector: &metav1.LabelSelector{
					MatchExpressions: []metav1.LabelSelectorRequirement{
						{
							Key:      "app",
							Values:   []string{jc.Name},
							Operator: metav1.LabelSelectorOpIn,
						},
					},
				},
				TopologyKey: "kubernetes.io/hostname",
			}
			// Append new anti-affinity term
			jc.Spec.Template.Spec.Affinity.PodAntiAffinity.RequiredDuringSchedulingIgnoredDuringExecution =
				append(jc.Spec.Template.Spec.Affinity.PodAntiAffinity.RequiredDuringSchedulingIgnoredDuringExecution, antiAffinityTerm)

		}
	}
	return jc
}

func (jc *JobConfig) WithImage(image string) *JobConfig {
	jc.Spec.Template.Spec.Containers[0].Image = image
	return jc
}

func (jc *JobConfig) WithImagePullPolicy(policy corev1.PullPolicy) *JobConfig {
	jc.Spec.Template.Spec.Containers[0].ImagePullPolicy = policy
	return jc
}

func (jc *JobConfig) WithRestartPolicy(policy corev1.RestartPolicy) *JobConfig {
	jc.Spec.Template.Spec.RestartPolicy = policy
	return jc
}

func (jc *JobConfig) WithCommandArgs(containerIndex int, args []string) *JobConfig {
	if containerIndex < len(jc.Spec.Template.Spec.Containers) {
		jc.Spec.Template.Spec.Containers[containerIndex].Args = args
	}
	return jc
}

func (jc *JobConfig) WithEnv(containerIndex int, name, value string) *JobConfig {
	if containerIndex < len(jc.Spec.Template.Spec.Containers[0].Env) {
		for i := range jc.Spec.Template.Spec.Containers[containerIndex].Env {
			env := &jc.Spec.Template.Spec.Containers[containerIndex].Env[i]
			if env.Name == name {
				env.Value = value
				return jc
			}
		}
	}
	jc.Spec.Template.Spec.Containers[containerIndex].Env = append(
		jc.Spec.Template.Spec.Containers[containerIndex].Env,
		corev1.EnvVar{
			Name:  name,
			Value: value,
		},
	)
	return jc
}

// WithServiceAccountName sets the service account for the job's pod template.
func (jc *JobConfig) WithServiceAccountName(name string) *JobConfig {
	jc.Spec.Template.Spec.ServiceAccountName = name
	return jc
}

// WithWorkloadIdentityLabel adds the Azure workload identity label to the job's pod template.
func (jc *JobConfig) WithWorkloadIdentityLabel() *JobConfig {
	if jc.Spec.Template.Labels == nil {
		jc.Spec.Template.Labels = make(map[string]string)
	}
	jc.Spec.Template.Labels["azure.workload.identity/use"] = "true"
	return jc
}

// DownwardAPIName adds a downward API of pod name to the job
func (jc *JobConfig) WithEnvDownwardAPIPodName() *JobConfig {
	downardAPI := corev1.EnvVar{
		Name: "POD_NAME",
		ValueFrom: &corev1.EnvVarSource{
			FieldRef: &corev1.ObjectFieldSelector{
				FieldPath: "metadata.name",
			},
		},
	}
	jc.Spec.Template.Spec.Containers[0].Env = append(jc.Spec.Template.Spec.Containers[0].Env, downardAPI)
	return jc
}

// WithEnvFromSecret adds an EnvFromSource that imports all keys from the provided Secret
// into environment variables for the specified container.
// The secret's name is used to reference it; namespace scoping is handled by the pod spec.
func (jc *JobConfig) WithEnvFromSecret(containerIndex int, secret *corev1.Secret) *JobConfig {
	// Validate container index
	if containerIndex < 0 || containerIndex >= len(jc.Spec.Template.Spec.Containers) {
		return jc
	}
	// Validate secret
	if secret == nil || secret.Name == "" {
		return jc
	}

	c := &jc.Spec.Template.Spec.Containers[containerIndex]
	c.EnvFrom = append(c.EnvFrom, corev1.EnvFromSource{
		SecretRef: &corev1.SecretEnvSource{
			LocalObjectReference: corev1.LocalObjectReference{Name: secret.Name},
		},
	})
	return jc
}
