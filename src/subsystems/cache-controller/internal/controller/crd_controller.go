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
	"sigs.k8s.io/controller-runtime/pkg/builder"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
	"sigs.k8s.io/controller-runtime/pkg/handler"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/yaml"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/Azure/dacs/cache-controller/internal/scaling"
)

// CacheReconciler reconciles a Cache object
type CacheReconciler struct {
	client.Client
	Scheme *runtime.Scheme
	Scaler *scaling.Reconciler
}

// Default RBAC rules added by kubebuilder
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches/status,verbs=get;update;patch
// +kubebuilder:rbac:groups=storage.azure.com,resources=caches/finalizers,verbs=update
//
// RBAC permissions are split into two rules per resource type:
// 1. Read/Create rule (no resourceNames): Includes 'list;watch' because controller-runtime
//    informers (created by .Owns() in SetupWithManager) need to list/watch ALL resources to
//    build their cache and detect changes. Informers cannot work with resourceNames restrictions.
// 2. Modify rule (with resourceNames): Restricts get/update/patch/delete to only the specific named
//    resources this controller manages for security.
//
// +kubebuilder:rbac:groups=apps,resources=statefulsets,verbs=create;list;watch
// +kubebuilder:rbac:groups=apps,resources=statefulsets,resourceNames=cacheserver,verbs=get;update;patch;delete
//
// +kubebuilder:rbac:groups="",resources=nodes,verbs=get;list;watch
//
// +kubebuilder:rbac:groups="",resources=services,verbs=create;list;watch
// +kubebuilder:rbac:groups="",resources=services,resourceNames=cacheserver;cacheserver-discovery,verbs=get;update;patch;delete
//
// +kubebuilder:rbac:groups="",resources=configmaps,verbs=create;list;watch
// +kubebuilder:rbac:groups="",resources=configmaps,resourceNames=cacheserver-discovery-config,verbs=get;update;patch;delete

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

	log.V(1).Info("Reconciling Cache resource")

	// Validate required fields are set
	if err := validateCacheSpec(&cache); err != nil {
		return ctrl.Result{}, fmt.Errorf("cache spec validation failed: %w", err)
	}

	numServers, err := r.resolveNumServers(ctx, &cache)
	if err != nil {
		return ctrl.Result{}, err
	}

	log.Info("Starting reconciliation",
		"desiredServers", numServers)

	// Reconcile StatefulSet
	log.V(1).Info("Reconciling StatefulSet")
	if err := r.reconcileStatefulSet(ctx, &cache, numServers); err != nil {
		log.Error(err, "Failed to reconcile StatefulSet")
		return ctrl.Result{}, fmt.Errorf("failed to reconcile StatefulSet: %w", err)
	}

	// Reconcile Services
	log.V(1).Info("Reconciling Services")
	if err := r.reconcileServices(ctx, &cache); err != nil {
		log.Error(err, "Failed to reconcile Services")
		return ctrl.Result{}, err
	}

	// Reconcile ConfigMap
	log.V(1).Info("Reconciling ConfigMap")
	if err := r.reconcileConfigMap(ctx, &cache, numServers); err != nil {
		log.Error(err, "Failed to reconcile ConfigMap")
		return ctrl.Result{}, err
	}

	// Update status after successful reconciliation
	log.V(1).Info("Updating Cache status")
	if err := r.updateCacheStatus(ctx, &cache, numServers); err != nil {
		log.Error(err, "Failed to update Cache status")
		return ctrl.Result{}, err
	}

	log.Info("Reconciliation completed successfully")

	return ctrl.Result{}, nil
}

// resolveNumServers determines the desired number of cache servers based on the scaling configuration.
func (r *CacheReconciler) resolveNumServers(ctx context.Context, cache *storagev1.Cache) (int32, error) {
	switch {
	case cache.Spec.Scaling.Static != nil:
		return cache.Spec.Scaling.Static.NumServers, nil
	case cache.Spec.Scaling.Dynamic != nil:
		numServers, err := r.Scaler.DesiredReplicas(ctx, cache)
		if err != nil {
			return 0, fmt.Errorf("failed to compute desired replicas: %w", err)
		}
		return numServers, nil
	default:
		return 0, fmt.Errorf("cache %s/%s does not have scaling configuration", cache.Namespace, cache.Name)
	}
}

func (r *CacheReconciler) reconcileStatefulSet(ctx context.Context, cache *storagev1.Cache, numServers int32) error {
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

	// Extract values from CRD spec
	// Defaults are applied by the mutating webhook at admission time.
	// Validation is done by validateCacheSpec called in Reconcile.

	// Storage configuration
	useEphemeral := false
	if cache.Spec.Storage.Ephemeral != nil {
		useEphemeral = *cache.Spec.Storage.Ephemeral
	}
	useHostPath := !useEphemeral
	// MountSsdPath needs to be set to a valid path even if using ephemeral storage
	nodeCachePath := storagev1.DefaultNodeCacheFolderPath
	if cache.Spec.Storage.HostPath != nil && cache.Spec.Storage.HostPath.NodeCacheFolderPath != "" {
		nodeCachePath = cache.Spec.Storage.HostPath.NodeCacheFolderPath
	}

	cfg := config.GetConfig()

	log.V(1).Info("Applying StatefulSet configuration",
		"replicas", numServers,
		"image", cfg.CacheServerImage,
		"port", *cache.Spec.Port,
		"metricsPort", *cache.Spec.MetricsPort,
		"ephemeralStorage", useEphemeral,
		"evictionEnabled", *cache.Spec.Eviction.EnableAccessTimeEviction,
		"evictionLastAccessThreshold", *cache.Spec.Eviction.LastAccessThreshold,
		"evictionSizeThreshold", *cache.Spec.Eviction.SizeThreshold)

	values := appValues{
		Namespace:                    cache.Namespace,
		NumServers:                   numServers,
		ManagedById:                "",
		EgressBandwidthInGb:          cache.Spec.EgressBandwidthLimitGbps,
		UseEphemeralStorage:          useEphemeral,
		NodeSelectorLabelKey:         *cache.Spec.NodeSelectorKey,
		NodeSelectorValue:            []string{*cache.Spec.NodeSelectorValue},
		Image:                        cfg.CacheServerImage,
		ServerThreads:                cache.Spec.ServerThreads,
		Port:                         int(*cache.Spec.Port),
		MetricsPort:                  int(*cache.Spec.MetricsPort),
		EnableAccessTimeEviction:     *cache.Spec.Eviction.EnableAccessTimeEviction,
		EvictionLastAccessThreshold:  *cache.Spec.Eviction.LastAccessThreshold,
		EvictionSizeThreshold:        *cache.Spec.Eviction.SizeThreshold,
		EvictionPassFrequency:        *cache.Spec.Eviction.PassFrequency,
		EvictionMaintainFreeSpace:    *cache.Spec.Eviction.EvictionMaintainFreeSpace,
		EvictionSizeTarget:           *cache.Spec.Eviction.SizeTarget,
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
		sts.Labels = desired.Labels
		sts.Spec = desired.Spec
		if err := controllerutil.SetControllerReference(cache, sts, r.Scheme); err != nil {
			return err
		}
		return nil
	})

	if err != nil {
		log.Error(err, "Failed to create or update StatefulSet",
			"statefulSetName", sts.Name,
			"desiredReplicas", numServers)
		return err
	}

	logResourceOperation(log, result, "StatefulSet", sts.Name, "replicas", numServers)

	return nil
}

func (r *CacheReconciler) reconcileServices(ctx context.Context, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)
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
			}{Namespace: cache.Namespace, Port: int(*cache.Spec.Port), MetricsPort: int(*cache.Spec.MetricsPort)},
		},
		{
			name:         "cacheserver-discovery",
			templatePath: "templates/cacheserver-discovery-service.yaml",
			values: struct {
				Namespace string
				Port      int
			}{Namespace: cache.Namespace, Port: int(*cache.Spec.Port)},
		},
	}

	// Reconciling each service
	for _, svc := range services {
		log.V(1).Info("Reconciling Service", "serviceName", svc.name)
		// Render desired Service from template
		var desired corev1.Service
		if err := renderTemplateInto(svc.templatePath, svc.values, &desired); err != nil {
			log.Error(err, "Failed to render service template",
				"serviceName", svc.name,
				"templatePath", svc.templatePath)
			return err
		}
		service := &corev1.Service{
			ObjectMeta: metav1.ObjectMeta{
				Name:      svc.name,
				Namespace: cache.Namespace,
			},
		}

		result, err := controllerutil.CreateOrUpdate(ctx, r.Client, service, func() error {
			service.Spec = desired.Spec
			service.Labels = desired.Labels
			if err := controllerutil.SetControllerReference(cache, service, r.Scheme); err != nil {
				return err
			}
			return nil
		})
		if err != nil {
			log.Error(err, "Failed to create or update Service",
				"serviceName", svc.name)
			return err
		}

		logResourceOperation(log, result, "Service", service.Name, "port", *cache.Spec.Port)
	}

	return nil
}

func (r *CacheReconciler) reconcileConfigMap(ctx context.Context, cache *storagev1.Cache, numServers int32) error {
	log := logf.FromContext(ctx)

	log.V(1).Info("Reconciling ConfigMap",
		"configMapName", "cacheserver-discovery-config",
		"numServers", numServers)

	values := struct {
		Namespace  string
		NumServers int32
	}{cache.Namespace, numServers}

	var desired corev1.ConfigMap
	if err := renderTemplateInto("templates/cacheserver-discovery-config.yaml", values, &desired); err != nil {
		log.Error(err, "Failed to render ConfigMap template",
			"numServers", numServers)
		return err
	}

	cm := &corev1.ConfigMap{
		ObjectMeta: metav1.ObjectMeta{
			Name:      "cacheserver-discovery-config",
			Namespace: cache.Namespace,
		},
	}

	result, err := controllerutil.CreateOrUpdate(ctx, r.Client, cm, func() error {
		cm.Labels = desired.Labels
		cm.Data = desired.Data
		if err := controllerutil.SetControllerReference(cache, cm, r.Scheme); err != nil {
			return err
		}
		return nil
	})

	if err != nil {
		log.Error(err, "Failed to create or update ConfigMap",
			"configMapName", cm.Name)
		return err
	}

	logResourceOperation(log, result, "ConfigMap", cm.Name, "numServers", numServers)

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

// logResourceOperation logs the result of a CreateOrUpdate operation in a consistent way
func logResourceOperation(log logr.Logger, result controllerutil.OperationResult, resourceType, name string, extraFields ...interface{}) {
	baseFields := []interface{}{"resourceName", name}
	if result == controllerutil.OperationResultCreated {
		log.Info("Created "+resourceType, append(baseFields, extraFields...)...)
	} else if result == controllerutil.OperationResultUpdated {
		log.Info("Updated "+resourceType, append(baseFields, extraFields...)...)
	} else {
		log.V(1).Info(resourceType+" unchanged", baseFields...)
	}
}

// updateCacheStatus updates the Cache status with current state and conditions.
// Uses a merge-patch so unchanged status is a no-op (no resourceVersion bump).
func (r *CacheReconciler) updateCacheStatus(ctx context.Context, cache *storagev1.Cache, desired int32) error {
	log := logf.FromContext(ctx)

	// Snapshot before mutations so MergeFrom can compute a minimal diff.
	patch := client.MergeFrom(cache.DeepCopy())

	var sts appsv1.StatefulSet
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
				"readyReplicas", current)
		} else if current > 0 {
			setStatusCondition(&cache.Status, metav1.Condition{
				Type:    "Ready",
				Status:  metav1.ConditionFalse,
				Reason:  "ReplicasNotReady",
				Message: fmt.Sprintf("%d/%d cache servers are ready", current, desired),
			})
			log.V(1).Info("Cache partially ready",
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
				"desiredReplicas", desired)
		}
	} else if apierrors.IsNotFound(err) {
		setStatusCondition(&cache.Status, metav1.Condition{
			Type:    "Ready",
			Status:  metav1.ConditionFalse,
			Reason:  "StatefulSetNotFound",
			Message: "Cache server StatefulSet does not exist yet",
		})
		log.V(1).Info("StatefulSet not found, will be created")
	} else {
		setStatusCondition(&cache.Status, metav1.Condition{
			Type:    "Ready",
			Status:  metav1.ConditionUnknown,
			Reason:  "StatefulSetError",
			Message: fmt.Sprintf("Failed to get StatefulSet: %v", err),
		})
		log.Error(err, "Failed to get StatefulSet for status update",
			"statefulSetName", "cacheserver")
	}

	cache.Status.DesiredServers = &desired
	cache.Status.CurrentServers = &current
	log.V(1).Info("Updating Cache status",
		"desiredServers", desired,
		"currentServers", current)
	return r.Status().Patch(ctx, cache, patch)
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

// validateCacheSpec validates that all required fields in the Cache spec are set.
// Defaults are applied by the mutating webhook at admission time.
func validateCacheSpec(cache *storagev1.Cache) error {
	if cache == nil {
		return fmt.Errorf("cache is nil")
	}
	if cache.Spec.Port == nil {
		return fmt.Errorf("spec.port is nil")
	}
	if cache.Spec.MetricsPort == nil {
		return fmt.Errorf("spec.metricsPort is nil")
	}
	if cache.Spec.Storage == nil {
		return fmt.Errorf("spec.storage is nil")
	}
	if cache.Spec.NodeSelectorKey == nil {
		return fmt.Errorf("spec.nodeSelectorKey is nil")
	}
	if cache.Spec.NodeSelectorValue == nil {
		return fmt.Errorf("spec.nodeSelectorValue is nil")
	}
	if cache.Spec.Eviction == nil {
		return fmt.Errorf("spec.eviction is nil")
	}
	if cache.Spec.Eviction.EnableAccessTimeEviction == nil {
		return fmt.Errorf("spec.eviction.enableAccessTimeEviction is nil")
	}
	if cache.Spec.Eviction.EvictionMaintainFreeSpace == nil {
		return fmt.Errorf("spec.eviction.evictionMaintainFreeSpace is nil")
	}
	if cache.Spec.Eviction.LastAccessThreshold == nil {
		return fmt.Errorf("spec.eviction.lastAccessThreshold is nil")
	}
	if cache.Spec.Eviction.SizeThreshold == nil {
		return fmt.Errorf("spec.eviction.sizeThreshold is nil")
	}
	if cache.Spec.Eviction.PassFrequency == nil {
		return fmt.Errorf("spec.eviction.passFrequency is nil")
	}
	if cache.Spec.Eviction.SizeTarget == nil {
		return fmt.Errorf("spec.eviction.sizeTarget is nil")
	}
	return nil
}

// SetupWithManager sets up the controller with the Manager.
func (r *CacheReconciler) SetupWithManager(ctx context.Context, mgr ctrl.Manager) error {
	// Register field indexes needed for scaling node lookups.
	if err := r.Scaler.RegisterIndexes(ctx, mgr); err != nil {
		return err
	}

	return ctrl.NewControllerManagedBy(mgr).
		For(&storagev1.Cache{}).
		Owns(&appsv1.StatefulSet{}).
		Owns(&corev1.Service{}).
		Owns(&corev1.ConfigMap{}).
		Watches(&corev1.Node{},
			handler.EnqueueRequestsFromMapFunc(r.Scaler.MapNodeToCaches),
			builder.WithPredicates(NodePredicate{}),
		).
		Named("cache").
		Complete(r)
}

//go:embed templates/*.yaml
var templateFS embed.FS
