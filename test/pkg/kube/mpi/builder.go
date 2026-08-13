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

package mpi

import (
	v2beta1 "github.com/kubeflow/mpi-operator/pkg/apis/kubeflow/v2beta1"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// New creates a new MPIJob builder with the given name and sensible defaults.
func New(name string) *MPIJob {
	job := &v2beta1.MPIJob{
		TypeMeta: metav1.TypeMeta{
			APIVersion: "kubeflow.org/v2beta1",
			Kind:       "MPIJob",
		},
		ObjectMeta: metav1.ObjectMeta{
			Name: name,
		},
		Spec: v2beta1.MPIJobSpec{
			MPIReplicaSpecs: map[v2beta1.MPIReplicaType]*v2beta1.ReplicaSpec{},
		},
	}
	return &MPIJob{MPIJob: job}
}

// WithName sets the metadata.name of the MPIJob
func (b *MPIJob) WithName(name string) *MPIJob {
	if b == nil || b.MPIJob == nil {
		return New(name)
	}
	b.ObjectMeta.Name = name
	return b
}

// WithNamespace sets the metadata.namespace of the MPIJob
func (b *MPIJob) WithNamespace(namespace string) *MPIJob {
	if b == nil || b.MPIJob == nil {
		return b
	}
	b.ObjectMeta.Namespace = namespace
	return b
}

// WithBackoffLimit sets spec.runPolicy.backoffLimit.
func (b *MPIJob) WithBackoffLimit(limit int32) *MPIJob {
	if b == nil || b.MPIJob == nil {
		return b
	}
	b.Spec.RunPolicy.BackoffLimit = &limit
	return b
}

// WithCleanPodPolicy sets spec.runPolicy.cleanPodPolicy.
func (b *MPIJob) WithCleanPodPolicy(policy v2beta1.CleanPodPolicy) *MPIJob {
	if b == nil || b.MPIJob == nil {
		return b
	}
	b.Spec.RunPolicy.CleanPodPolicy = &policy
	return b
}

// WithTTLSecondsAfterFinished sets spec.runPolicy.ttlSecondsAfterFinished.
func (b *MPIJob) WithTTLSecondsAfterFinished(ttl int32) *MPIJob {
	if b == nil || b.MPIJob == nil {
		return b
	}
	b.Spec.RunPolicy.TTLSecondsAfterFinished = &ttl
	return b
}

// ensureReplicaSpec ensures a ReplicaSpec exists for the given replica type.
func (b *MPIJob) ensureReplicaSpec(rt v2beta1.MPIReplicaType) *v2beta1.ReplicaSpec {
	if b == nil || b.MPIJob == nil {
		b = New("not-named")
	}
	if b.Spec.MPIReplicaSpecs == nil {
		b.Spec.MPIReplicaSpecs = map[v2beta1.MPIReplicaType]*v2beta1.ReplicaSpec{}
	}
	spec, ok := b.Spec.MPIReplicaSpecs[rt]
	if !ok || spec == nil {
		spec = &v2beta1.ReplicaSpec{
			Template: corev1.PodTemplateSpec{
				Spec: corev1.PodSpec{
					RestartPolicy: corev1.RestartPolicyNever,
					Containers:    []corev1.Container{},
				},
			},
		}
		b.Spec.MPIReplicaSpecs[rt] = spec
	}
	// Ensure a single container exists for convenience.
	if len(spec.Template.Spec.Containers) == 0 {
		name := ""
		switch rt {
		case v2beta1.MPIReplicaTypeLauncher:
			name = "launcher"
		case v2beta1.MPIReplicaTypeWorker:
			name = "worker"
		default:
			name = "container"
		}
		spec.Template.Spec.Containers = []corev1.Container{{
			Name: name,
		}}
	}
	return spec
}

// WithLauncherReplicas sets replicas for the Launcher.
func (b *MPIJob) WithLauncherReplicas(replicas int32) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	spec.Replicas = &replicas
	return b
}

// WithLauncherImage sets image and pull policy for the launcher container.
func (b *MPIJob) WithLauncherImage(image string, pullPolicy corev1.PullPolicy) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	c := &spec.Template.Spec.Containers[0]
	c.Image = image
	c.ImagePullPolicy = pullPolicy
	return b
}

// WithLauncherEnv appends environment variables to the launcher container.
func (b *MPIJob) WithLauncherEnv(env corev1.EnvVar) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	c := &spec.Template.Spec.Containers[0]
	// Update and return env vars
	for index, existEnv := range c.Env {
		if existEnv.Name == env.Name {
			c.Env[index].Value = env.Value
			c.Env[index].ValueFrom = env.ValueFrom
			return b
		}
	}
	c.Env = append(c.Env, env)
	return b
}

// WithLauncherCommand sets command for the launcher container.
func (b *MPIJob) WithLauncherCommand(cmd ...string) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	c := &spec.Template.Spec.Containers[0]
	c.Command = append([]string{}, cmd...)
	return b
}

// WithLauncherArgs sets args for the launcher container.
func (b *MPIJob) WithLauncherArgs(args ...string) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	c := &spec.Template.Spec.Containers[0]
	c.Args = append([]string{}, args...)
	return b
}

func (b *MPIJob) WithServiceAccountName(name string) *MPIJob {
	b.WithLauncherServiceAccount(name).
		WithWorkerServiceAccount(name)
	return b
}

func (b *MPIJob) WithWorkloadIdentityLabel() *MPIJob {
	launcherSpec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	if launcherSpec.Template.Labels == nil {
		launcherSpec.Template.Labels = make(map[string]string)
	}
	workerSpec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	if workerSpec.Template.Labels == nil {
		workerSpec.Template.Labels = make(map[string]string)
	}
	launcherSpec.Template.Labels["azure.workload.identity/use"] = "true"
	workerSpec.Template.Labels["azure.workload.identity/use"] = "true"
	return b
}

// WithLauncherServiceAccount sets the service account name for the Launcher pod.
func (b *MPIJob) WithLauncherServiceAccount(name string) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeLauncher)
	spec.Template.Spec.ServiceAccountName = name
	return b
}

// WithWorkerServiceAccount sets the service account name for the Worker pod.
func (b *MPIJob) WithWorkerServiceAccount(name string) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	spec.Template.Spec.ServiceAccountName = name
	return b
}

// WithWorkerReplicas sets replicas for the Worker.
func (b *MPIJob) WithWorkerReplicas(replicas int32) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	spec.Replicas = &replicas
	return b
}

// WithWorkerImage sets image and pull policy for the worker container.
func (b *MPIJob) WithWorkerImage(image string, pullPolicy corev1.PullPolicy) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	c := &spec.Template.Spec.Containers[0]
	c.Image = image
	c.ImagePullPolicy = pullPolicy
	return b
}

// WithWorkerEnv appends environment variables to the worker container.
func (b *MPIJob) WithWorkerEnv(env corev1.EnvVar) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	c := &spec.Template.Spec.Containers[0]
	// Update and return env vars
	for index, existEnv := range c.Env {
		if existEnv.Name == env.Name {
			c.Env[index].Value = env.Value
			c.Env[index].ValueFrom = env.ValueFrom
			return b
		}
	}
	c.Env = append(c.Env, env)
	return b
}

// WithWorkerPorts appends ports to the worker container.
func (b *MPIJob) WithWorkerPorts(ports ...corev1.ContainerPort) *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	c := &spec.Template.Spec.Containers[0]
	c.Ports = append(c.Ports, ports...)
	return b
}

// WithWorkerPodAntiAffinityForJob ensures pod anti-affinity preventing multiple workers on same node,
// keyed to label training.kubeflow.org/job-name matching the job name.
func (b *MPIJob) WithWorkerPodAntiAffinityForJob() *MPIJob {
	spec := b.ensureReplicaSpec(v2beta1.MPIReplicaTypeWorker)
	labelSelector := &metav1.LabelSelector{
		MatchExpressions: []metav1.LabelSelectorRequirement{
			{
				Key:      "training.kubeflow.org/job-name",
				Operator: metav1.LabelSelectorOpIn,
				Values:   []string{b.ObjectMeta.Name},
			},
			{
				Key:      "training.kubeflow.org/job-role",
				Operator: metav1.LabelSelectorOpIn,
				Values:   []string{"worker"},
			},
		},
	}
	spec.Template.Spec.Affinity = &corev1.Affinity{
		PodAntiAffinity: &corev1.PodAntiAffinity{
			RequiredDuringSchedulingIgnoredDuringExecution: []corev1.PodAffinityTerm{
				{
					LabelSelector: labelSelector,
					TopologyKey:   "kubernetes.io/hostname",
				},
			},
		},
	}
	return b
}

// DownwardEnv helpers for JOB_NAME/POD_NAMESPACE/POD_NAME consistent with mpi-job.yaml.
func DownwardEnvJobName() corev1.EnvVar {
	return corev1.EnvVar{
		Name: "JOB_NAME",
		ValueFrom: &corev1.EnvVarSource{
			FieldRef: &corev1.ObjectFieldSelector{FieldPath: "metadata.labels['training.kubeflow.org/job-name']"},
		},
	}
}

func DownwardEnvPodNamespace() corev1.EnvVar {
	return corev1.EnvVar{
		Name: "POD_NAMESPACE",
		ValueFrom: &corev1.EnvVarSource{
			FieldRef: &corev1.ObjectFieldSelector{FieldPath: "metadata.namespace"},
		},
	}
}

func DownwardEnvPodName() corev1.EnvVar {
	return corev1.EnvVar{
		Name: "POD_NAME",
		ValueFrom: &corev1.EnvVarSource{
			FieldRef: &corev1.ObjectFieldSelector{FieldPath: "metadata.name"},
		},
	}
}

// Convenience env var creators for common variables used in mpi-job.yaml.
func EnvVar(name, value string) corev1.EnvVar { return corev1.EnvVar{Name: name, Value: value} }

// Build returns the underlying MPIJob object.
func (b *MPIJob) Build() *v2beta1.MPIJob { return b.MPIJob }
