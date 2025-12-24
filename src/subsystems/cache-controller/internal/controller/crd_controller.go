/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"bytes"
	"context"
	"embed"
	"encoding/json"
	"text/template"

	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/yaml"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
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
// RBAC to manage statefulsets with name "cacheserver"
// +kubebuilder:rbac:resources=statefulsets,resourceNames=cacheserver,verbs=get;list;watch;create;update;patch;delete
//
// RBAC to manage services with names "cacheserver" or "cacheserver-discovery"
// +kubebuilder:rbac:resources=services,resourceNames=cacheserver,verbs=get;list;watch;create;update;patch;delete
// +kubebuilder:rbac:resources=services,resourceNames=cacheserver-discovery,verbs=get;list;watch;create;update;patch;delete
//
// RBAC to manage configmaps with name "cacheserver-discovery-config"
// +kubebuilder:rbac:resources=configmaps,resourceNames=cacheserver-discovery-config,verbs=get;list;watch;create;update;patch;delete
//
// RBAC to manage all configmaps in "cache-controller-system" namespace
// +kubebuilder:rbac:namespace=cache-controller-system,resources=configmaps,verbs=get;list;watch;create;update;patch;delete

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

	// Reconcile StatefulSet
	if err := r.reconcileStatefulSet(ctx, &cache); err != nil {
		log.Error(err, "Failed to reconcile StatefulSet")
		return ctrl.Result{}, err
	}

	// Reconcile Services
	var serviceList corev1.ServiceList
	if err := r.List(ctx, &serviceList, client.InNamespace(req.Namespace)); err != nil {
		log.Error(err, "Failed to list Services")
		return ctrl.Result{}, err
	}
	if err := r.reconcileServices(ctx, serviceList, &cache); err != nil {
		log.Error(err, "Failed to reconcile Services")
		return ctrl.Result{}, err
	}

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
	if cache.Spec.Scaling.Fixed != nil {
		numServers = cache.Spec.Scaling.Fixed.NumServers
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

	values := appValues{
		Namespace:                    cache.Namespace,
		NumServers:                   numServers,
		ManagedById:                "",
		EgressBandwidthInGb:          egressGb,
		UseEphemeralStorage:          useEphemeral,
		NodeSelectorLabelKey:         nodeKey,
		NodeSelectorValue:            []string{nodeVal},
		Image:                        "cacheserver:local", // TODO: pick from config
		ServerThreads:                cache.Spec.ServerThreads,
		Port:                         int(port),
		MetricsPort:                  int(mport),
		EnableAccessTimeEviction:     enableEvict,
		EvictionLastAccessThreshold:  "30d", // TODO: pick from config
		EvictionSizeThreshold:        "90",  // TODO: pick from config
		EvictionPassFrequency:        "30m", // TODO: pick from config
		EvictionMaintainFreeSpace:    enableEvictMaintainFreeSpace,
		EvictionSizeTarget:           "80", // TODO: pick from config
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
		log.Error(err, "Failed to render cacheserver statefulset template")
		return err
	}
	// Set owner reference
	if err := controllerutil.SetControllerReference(cache, &desired, r.Scheme); err != nil {
		return err
	}
	// Create or Update StatefulSet
	var existing appsv1.StatefulSet
	err := r.Get(ctx, types.NamespacedName{Name: desired.Name, Namespace: desired.Namespace}, &existing)
	if err != nil && apierrors.IsNotFound(err) {
		log.Info("Creating StatefulSet", "Name", desired.Name, "Namespace", desired.Namespace, "Replicas", *desired.Spec.Replicas)
		if err := r.Create(ctx, &desired); err != nil {
			return err
		}
	} else if err != nil {
		return err
	} else {
		// TODO: Update existing StatefulSet if needed
		log.V(1).Info("StatefulSet already exists, skipping update", "Name", existing.Name)
	}
	return nil
}

func (r *CacheReconciler) reconcileServices(ctx context.Context, serviceList corev1.ServiceList, cache *storagev1.Cache) error {
	log := logf.FromContext(ctx)

	var existingCacheService *corev1.Service
	for _, service := range serviceList.Items {
		if service.Name == "cacheserver" {
			existingCacheService = &service
			break
		}
	}

	if existingCacheService == nil {
		// Render desired Service from template using helper
		values := struct {
			Namespace   string
			Port        int
			MetricsPort int
		}{Namespace: cache.Namespace, Port: int(*cache.Spec.Port), MetricsPort: int(*cache.Spec.MetricsPort)}
		var desired corev1.Service
		if err := renderTemplateInto("templates/cacheserver-service.yaml", values, &desired); err != nil {
			log.Error(err, "Failed to render cacheserver service template")
			return err
		}
		// Set owner reference
		if err := controllerutil.SetControllerReference(cache, &desired, r.Scheme); err != nil {
			return err
		}
		// Create
		log.Info("Creating cacheserver service", "Service.Name", desired.Name)
		if err := r.Create(ctx, &desired); err != nil {
			return err
		}
	} else {
		// TODO: update existing service if ports changed
		log.V(1).Info("Service 'cacheserver' already exists, skipping creation")
	}

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
