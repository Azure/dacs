/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"bytes"
	"context"
	"embed"
	"encoding/json"
	"fmt"
	"text/template"

	"github.com/go-logr/logr"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/yaml"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
)

// CacheReconciler reconciles a Cache object
type CacheReconciler struct {
	client.Client
	Scheme *runtime.Scheme
}

// Default RBAC rules added by kubebuilder
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches/status,verbs=get;update;patch
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches/finalizers,verbs=update
//
// RBAC to manage statefulsets - split into create (no resourceNames) and other verbs (with resourceNames)
// +kubebuilder:rbac:groups=apps,resources=statefulsets,verbs=create
// +kubebuilder:rbac:groups=apps,resources=statefulsets,resourceNames=cacheserver,verbs=get;list;watch;update;patch;delete
//
// RBAC to manage services - split into create (no resourceNames) and other verbs (with resourceNames)
// +kubebuilder:rbac:groups="",resources=services,verbs=create
// +kubebuilder:rbac:groups="",resources=services,resourceNames=cacheserver;cacheserver-discovery,verbs=get;list;watch;update;patch;delete
//
// RBAC to manage configmaps - split into create (no resourceNames) and other verbs (with resourceNames)
// +kubebuilder:rbac:groups="",resources=configmaps,verbs=create
// +kubebuilder:rbac:groups="",resources=configmaps,resourceNames=cacheserver-discovery-config,verbs=get;list;watch;update;patch;delete

// Reconcile is part of the main kubernetes reconciliation loop which aims to
// move the current state of the cluster closer to the desired state.
//
// For more details, check Reconcile and its Result here:
// - https://pkg.go.dev/sigs.k8s.io/controller-runtime@v0.22.4/pkg/reconcile
func (r *CacheReconciler) Reconcile(ctx context.Context, req ctrl.Request) (ctrl.Result, error) {
	log := logf.FromContext(ctx)

	// Get the Cache instance
	var cache storagev1.Cache
	if err := r.Get(ctx, req.NamespacedName, &cache); err != nil {
		if apierrors.IsNotFound(err) {
			// If the custom resource is not found then it usually means that it was deleted or not created
			// In this way, we will stop the reconciliation
			log.Info("Cache resource not found. Ignoring since object must be deleted")
			return ctrl.Result{}, nil
		}
		// Error reading the object - requeue the request.
		log.Error(err, "Failed to get Cache")
		return ctrl.Result{}, err
	}

	log.V(1).Info("Reconciling Cache resource", "Cache.Namespace", cache.Namespace, "Cache.Name", cache.Name)

	numServers := int32(0)
	if cache.Spec.Scaling.Static != nil {
		numServers = cache.Spec.Scaling.Static.NumServers
	}
	log.Info("Starting reconciliation",
		"namespace", cache.Namespace,
		"name", cache.Name,
		"desiredServers", numServers)

	// Reconcile StatefulSet
	log.V(1).Info("Reconciling StatefulSet", "namespace", cache.Namespace)
	if err := r.reconcileStatefulSet(ctx, &cache); err != nil {
		log.Error(err, "Failed to reconcile StatefulSet", "namespace", cache.Namespace, "name", cache.Name)
		return ctrl.Result{}, err
	}

	// Reconcile Services
	log.V(1).Info("Reconciling Services", "namespace", cache.Namespace)
	var serviceList corev1.ServiceList
	if err := r.List(ctx, &serviceList, client.InNamespace(req.Namespace)); err != nil {
		log.Error(err, "Failed to list Services", "namespace", req.Namespace)
		return ctrl.Result{}, err
	}
	if err := r.reconcileServices(ctx, serviceList, &cache); err != nil {
		log.Error(err, "Failed to reconcile Services", "namespace", cache.Namespace, "name", cache.Name)
		return ctrl.Result{}, err
	}

	// Reconcile ConfigMap
	log.V(1).Info("Reconciling ConfigMap", "namespace", cache.Namespace)
	if err := r.reconcileConfigMap(ctx, &cache); err != nil {
		log.Error(err, "Failed to reconcile ConfigMap", "namespace", cache.Namespace, "name", cache.Name)
		return ctrl.Result{}, err
	}

	// Update status after successful reconciliation
	log.V(1).Info("Updating Cache status", "namespace", cache.Namespace)
	if err := r.updateCacheStatus(ctx, &cache); err != nil {
		log.Error(err, "Failed to update Cache status", "namespace", cache.Namespace, "name", cache.Name)
		return ctrl.Result{}, err
	}

	log.Info("Reconciliation completed successfully", "namespace", cache.Namespace, "name", cache.Name)

	return ctrl.Result{}, nil
}

func (r *CacheReconciler) reconcileStatefulSet(ctx context.Context, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)

	// Render desired StatefulSet from richer app template using helper
	type appValues struct {
		Namespace                    string
		NumServers                   int32
		ManagedById                string
		EgressBandwidthInGb          *int32
		UseEphemeralStorage          bool
		NodeSelectorLabelKey         string
		NodeSelectorValue            []string
		Image                        string
		ServerThreads                int32
		Port                         int
		MetricsPort                  int
		EnableAccessTimeEviction     bool
		EvictionLastAccessThreshold  string
		EvictionSizeThreshold        string
		EvictionPassFrequency        string
		EvictionMaintainFreeSpace    bool
		EvictionSizeTarget           string
		InfinibandResourceLimitCount *int32
		InfinibandResourceLimitName  *string
		MemoryLimitMB                *int32
		CpuLimit                     *int32
		MountSsdPath                 string
		Name                         string
		ServerSizeInGB               int32
		NodeCacheFolderPath          string
		UseHostPath                  bool
	}

	// Safely derive values from CRD (with defaults)
	numServers := int32(0)
	if cache.Spec.Scaling.Static != nil {
		numServers = cache.Spec.Scaling.Static.NumServers
	}
	port := int32(9065)
	if cache.Spec.Port != nil {
		port = *cache.Spec.Port
	}
	mport := int32(9096)
	if cache.Spec.MetricsPort != nil {
		mport = *cache.Spec.MetricsPort
	}
	useEphemeral := false
	if cache.Spec.Storage != nil && cache.Spec.Storage.Ephemeral != nil {
		useEphemeral = *cache.Spec.Storage.Ephemeral
	}
	nodeKey := ""
	if cache.Spec.NodeSelectorKey != nil {
		nodeKey = *cache.Spec.NodeSelectorKey
	}
	nodeVal := ""
	if cache.Spec.NodeSelectorValue != nil {
		nodeVal = *cache.Spec.NodeSelectorValue
	}
	enableEvict := true
	if cache.Spec.Eviction != nil {
		enableEvict = boolValueOrDefault(cache.Spec.Eviction.EnableAccessTimeEviction, true)
	}
	enableEvictMaintainFreeSpace := true
	if cache.Spec.Eviction != nil && cache.Spec.Eviction.EvictionMaintainFreeSpace != nil {
		enableEvictMaintainFreeSpace = *cache.Spec.Eviction.EvictionMaintainFreeSpace
	}
	useHostPath := !useEphemeral
	nodeCachePath := "/var/lib/ssd/cacheserver"
	if cache.Spec.Storage != nil && cache.Spec.Storage.HostPath != nil && cache.Spec.Storage.HostPath.NodeCacheFolderPath != "" {
		nodeCachePath = cache.Spec.Storage.HostPath.NodeCacheFolderPath
	}
	var egressGb *int32
	if cache.Spec.EgressBandwidthLimitGbps != nil {
		egressGb = cache.Spec.EgressBandwidthLimitGbps
	}

	// Get eviction config values
	evictionLastAccess := "30d"
	evictionSizeThreshold := "90"
	evictionPassFreq := "1h"
	evictionSizeTarget := "90"
	if cache.Spec.Eviction != nil {
		evictionLastAccess = stringValueOrDefault(cache.Spec.Eviction.LastAccessThreshold, "30d")
		evictionSizeThreshold = stringValueOrDefault(cache.Spec.Eviction.SizeThreshold, "90")
		evictionPassFreq = stringValueOrDefault(cache.Spec.Eviction.PassFrequency, "1h")
		evictionSizeTarget = stringValueOrDefault(cache.Spec.Eviction.SizeTarget, "90")
	}

	cfg := config.GetConfig()

	log.V(1).Info("Applying StatefulSet configuration",
		"replicas", numServers,
		"image", cfg.CacheServerImage,
		"port", port,
		"metricsPort", mport,
		"ephemeralStorage", useEphemeral,
		"evictionEnabled", enableEvict,
		"evictionLastAccessThreshold", evictionLastAccess,
		"evictionSizeThreshold", evictionSizeThreshold)

	values := appValues{
		Namespace:                    cache.Namespace,
		NumServers:                   numServers,
		ManagedById:                "",
		EgressBandwidthInGb:          egressGb,
		UseEphemeralStorage:          useEphemeral,
		NodeSelectorLabelKey:         nodeKey,
		NodeSelectorValue:            []string{nodeVal},
		Image:                        cfg.CacheServerImage,
		ServerThreads:                cache.Spec.ServerThreads,
		Port:                         int(port),
		MetricsPort:                  int(mport),
		EnableAccessTimeEviction:     enableEvict,
		EvictionLastAccessThreshold:  evictionLastAccess,
		EvictionSizeThreshold:        evictionSizeThreshold,
		EvictionPassFrequency:        evictionPassFreq,
		EvictionMaintainFreeSpace:    enableEvictMaintainFreeSpace,
		EvictionSizeTarget:           evictionSizeTarget,
		InfinibandResourceLimitCount: cache.Spec.InfinibandResourceLimitCount,
		InfinibandResourceLimitName:  cache.Spec.InfinibandResourceLimitName,
		MemoryLimitMB:                cache.Spec.MemoryLimitMB,
		CpuLimit:                     cache.Spec.CpuLimit,
		MountSsdPath:                 nodeCachePath,
		Name:                         cache.Name,
		ServerSizeInGB:               cache.Spec.ServerSizeInGB,
		NodeCacheFolderPath:          nodeCachePath,
		UseHostPath:                  useHostPath,
	}
	var desired appsv1.StatefulSet
	if err := renderTemplateInto("templates/cacheserver-app.yaml", values, &desired); err != nil {
		log.Error(err, "Failed to render cacheserver statefulset template",
			"namespace", cache.Namespace,
			"numServers", numServers,
			"image", values.Image)
		return err
	}
	// Create or Update StatefulSet
	sts := &appsv1.StatefulSet{
		ObjectMeta: metav1.ObjectMeta{
			Name:      desired.Name,
			Namespace: desired.Namespace,
		},
	}

	result, err := controllerutil.CreateOrUpdate(ctx, r.Client, sts, func() error {
		sts.Spec = desired.Spec
		if err := controllerutil.SetControllerReference(cache, sts, r.Scheme); err != nil {
			return err
		}
		return nil
	})

	if err != nil {
		log.Error(err, "Failed to create or update StatefulSet",
			"namespace", cache.Namespace,
			"statefulSetName", sts.Name,
			"desiredReplicas", numServers)
		return err
	}

	logResourceOperation(log, result, "StatefulSet", sts.Namespace, sts.Name, "replicas", numServers)

	return nil
}

func (r *CacheReconciler) reconcileServices(ctx context.Context, serviceList corev1.ServiceList, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)

	// Get port values with defaults to avoid nil pointer dereference
	port := getPortWithDefault(cache.Spec.Port, 9065)
	metricsPort := getPortWithDefault(cache.Spec.MetricsPort, 9096)

	services := []struct {
		name         string
		templatePath string
		values       interface{}
	}{
		{
			name:         "cacheserver",
			templatePath: "templates/cacheserver-service.yaml",
			values: struct {
				Namespace   string
				Port        int
				MetricsPort int
			}{Namespace: cache.Namespace, Port: int(port), MetricsPort: int(metricsPort)},
		},
		{
			name:         "cacheserver-discovery",
			templatePath: "templates/cacheserver-discovery-service.yaml",
			values: struct {
				Namespace string
				Port      int
			}{Namespace: cache.Namespace, Port: int(port)},
		},
	}

	// Reconciling each service
	for _, svc := range services {
		log.V(1).Info("Reconciling Service", "serviceName", svc.name, "namespace", cache.Namespace)
		// Render desired Service from template
		var desiredSpec corev1.ServiceSpec
		var desired corev1.Service
		if err := renderTemplateInto(svc.templatePath, svc.values, &desired); err != nil {
			log.Error(err, "Failed to render service template",
				"serviceName", svc.name,
				"namespace", cache.Namespace,
				"templatePath", svc.templatePath)
			return err
		}
		desiredSpec = desired.Spec

		service := &corev1.Service{
			ObjectMeta: metav1.ObjectMeta{
				Name:      svc.name,
				Namespace: cache.Namespace,
			},
		}

		result, err := controllerutil.CreateOrUpdate(ctx, r.Client, service, func() error {
			service.Spec = desiredSpec
			if err := controllerutil.SetControllerReference(cache, service, r.Scheme); err != nil {
				return err
			}
			return nil
		})
		if err != nil {
			log.Error(err, "Failed to create or update Service",
				"serviceName", svc.name,
				"namespace", cache.Namespace)
			return err
		}

		logResourceOperation(log, result, "Service", service.Namespace, service.Name, "port", port)
	}

	return nil
}

func (r *CacheReconciler) reconcileConfigMap(ctx context.Context, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)

	numServers := int32(0)
	if cache.Spec.Scaling.Static != nil {
		numServers = cache.Spec.Scaling.Static.NumServers
	}

	log.V(1).Info("Reconciling ConfigMap",
		"configMapName", "cacheserver-discovery-config",
		"namespace", cache.Namespace,
		"numServers", numServers)

	values := struct {
		Namespace  string
		NumServers int32
	}{cache.Namespace, numServers}

	var desired corev1.ConfigMap
	if err := renderTemplateInto("templates/cacheserver-discovery-config.yaml", values, &desired); err != nil {
		log.Error(err, "Failed to render ConfigMap template",
			"namespace", cache.Namespace,
			"numServers", numServers)
		return err
	}
	desiredData := desired.Data

	cm := &corev1.ConfigMap{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "cacheserver-discovery-config",
			Namespace: cache.Namespace,
		},
	}

	result, err := controllerutil.CreateOrUpdate(ctx, r.Client, cm, func() error {
		cm.Data = desiredData
		if err := controllerutil.SetControllerReference(cache, cm, r.Scheme); err != nil {
			return err
		}
		return nil
	})

	if err != nil {
		log.Error(err, "Failed to create or update ConfigMap",
			"namespace", cache.Namespace,
			"configMapName", cm.Name)
		return err
	}

	logResourceOperation(log, result, "ConfigMap", cm.Namespace, cm.Name, "numServers", numServers)

	return nil
}

// renderTemplateInto loads a YAML template, executes it with values, and decodes into out (typed k8s object pointer)
func renderTemplateInto(path string, values interface{}, out interface{}) error {
	tmplBytes, err := templateFS.ReadFile(path)
	if err != nil {
		return err
	}
	// Template helper functions used by cacheserver-app.yaml
	funcMap := template.FuncMap{
		"add": func(a, b int32) int32 { return a + b },
		"mulAll": func(nums ...int32) int64 {
			var res int64 = 1
			for _, n := range nums {
				res *= int64(n)
			}
			return res
		},
	}
	t, err := template.New("tpl").Funcs(funcMap).Parse(string(tmplBytes))
	if err != nil {
		return err
	}
	var buf bytes.Buffer
	if err := t.Execute(&buf, values); err != nil {
		return err
	}
	jsonBytes, err := yaml.YAMLToJSON(buf.Bytes())
	if err != nil {
		return err
	}
	if err := json.Unmarshal(jsonBytes, out); err != nil {
		return err
	}
	return nil
}

// boolValueOrDefault returns the dereferenced value if non-nil, otherwise the provided default.
func boolValueOrDefault(v *bool, def bool) bool {
	if v == nil {
		return def
	}
	return *v
}

// stringValueOrDefault returns the dereferenced value if non-nil, otherwise the provided default.
func stringValueOrDefault(v *string, def string) string {
	if v == nil {
		return def
	}
	return *v
}

// logResourceOperation logs the result of a CreateOrUpdate operation in a consistent way
func logResourceOperation(log logr.Logger, result controllerutil.OperationResult, resourceType, namespace, name string, extraFields ...interface{}) {
	baseFields := []interface{}{"namespace", namespace, "name", name}
	if result == controllerutil.OperationResultCreated {
		log.Info("Created "+resourceType, append(baseFields, extraFields...)...)
	} else if result == controllerutil.OperationResultUpdated {
		log.Info("Updated "+resourceType, append(baseFields, extraFields...)...)
	} else {
		log.V(1).Info(resourceType+" unchanged", baseFields...)
	}
}

// updateCacheStatus updates the Cache status with current state and conditions.
func (r *CacheReconciler) updateCacheStatus(ctx context.Context, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)

	var sts appsv1.StatefulSet
	desired := int32(0)
	if cache.Spec.Scaling.Static != nil {
		desired = cache.Spec.Scaling.Static.NumServers
	}
	current := int32(0)

	err := r.Get(ctx, types.NamespacedName{Name: "cacheserver", Namespace: cache.Namespace}, &sts)
	if err == nil {
		current = sts.Status.ReadyReplicas

		// Log all replica counts for debugging:
		// - readyReplicas: Pods that are healthy and serving traffic
		// - desiredReplicas: Target count from Cache spec
		// - updatedReplicas: Pods running the latest template/image
		// - currentReplicas: Total pods that exist (running, pending, or failing)
		log.V(1).Info("StatefulSet status check",
			"namespace", cache.Namespace,
			"readyReplicas", current,
			"desiredReplicas", desired,
			"updatedReplicas", sts.Status.UpdatedReplicas,
			"currentReplicas", sts.Status.CurrentReplicas)

		// Set Ready condition based on replicas
		if current == desired {
			setStatusCondition(&cache.Status, metav1.Condition{
				Type:    "Ready",
				Status:  metav1.ConditionTrue,
				Reason:  "AllReplicasReady",
				Message: fmt.Sprintf("All %d cache servers are ready", desired),
			})
			log.V(1).Info("Cache is ready",
				"namespace", cache.Namespace,
				"name", cache.Name,
				"readyReplicas", current)
		} else if current > 0 {
			setStatusCondition(&cache.Status, metav1.Condition{
				Type:    "Ready",
				Status:  metav1.ConditionFalse,
				Reason:  "ReplicasNotReady",
				Message: fmt.Sprintf("%d/%d cache servers are ready", current, desired),
			})
			log.V(1).Info("Cache partially ready",
				"namespace", cache.Namespace,
				"name", cache.Name,
				"readyReplicas", current,
				"desiredReplicas", desired)
		} else {
			setStatusCondition(&cache.Status, metav1.Condition{
				Type:    "Ready",
				Status:  metav1.ConditionFalse,
				Reason:  "NoReplicasReady",
				Message: "No cache servers are ready yet",
			})
			log.V(1).Info("Cache not ready yet",
				"namespace", cache.Namespace,
				"name", cache.Name,
				"desiredReplicas", desired)
		}
	} else if apierrors.IsNotFound(err) {
		setStatusCondition(&cache.Status, metav1.Condition{
			Type:    "Ready",
			Status:  metav1.ConditionFalse,
			Reason:  "StatefulSetNotFound",
			Message: "Cache server StatefulSet does not exist yet",
		})
		log.V(1).Info("StatefulSet not found, will be created",
			"namespace", cache.Namespace,
			"name", cache.Name)
	} else {
		setStatusCondition(&cache.Status, metav1.Condition{
			Type:    "Ready",
			Status:  metav1.ConditionUnknown,
			Reason:  "StatefulSetError",
			Message: fmt.Sprintf("Failed to get StatefulSet: %v", err),
		})
		log.Error(err, "Failed to get StatefulSet for status update",
			"namespace", cache.Namespace,
			"statefulSetName", "cacheserver")
	}

	cache.Status.DesiredServers = &desired
	cache.Status.CurrentServers = &current
	log.V(1).Info("Updating Cache status",
		"namespace", cache.Namespace,
		"name", cache.Name,
		"desiredServers", desired,
		"currentServers", current)
	return r.Status().Update(ctx, cache)
}

// setStatusCondition sets or updates a condition in the Cache status.
func setStatusCondition(status *storagev1.CacheStatus, condition metav1.Condition) {
	condition.LastTransitionTime = metav1.Now()

	// Find existing condition of same type
	for i, existing := range status.Conditions {
		if existing.Type == condition.Type {
			// Only update LastTransitionTime if status changed
			if existing.Status != condition.Status {
				status.Conditions[i] = condition
			} else {
				// Keep old transition time if status unchanged
				condition.LastTransitionTime = existing.LastTransitionTime
				status.Conditions[i] = condition
			}
			return
		}
	}

	// Condition doesn't exist, add it
	status.Conditions = append(status.Conditions, condition)
}

// getPortWithDefault returns the port value or default if nil.
func getPortWithDefault(port *int32, defaultPort int32) int32 {
	if port == nil {
		return defaultPort
	}
	return *port
}

// SetupWithManager sets up the controller with the Manager.
func (r *CacheReconciler) SetupWithManager(mgr ctrl.Manager) error {

	return ctrl.NewControllerManagedBy(mgr).
		For(&storagev1.Cache{}).
		Owns(&appsv1.StatefulSet{}).
		Owns(&corev1.Service{}).
		Owns(&corev1.ConfigMap{}).
		Named("cache").
		Complete(r)
}

//go:embed templates/*.yaml
var templateFS embed.FS
