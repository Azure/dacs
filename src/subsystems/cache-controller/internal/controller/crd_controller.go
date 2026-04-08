/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package controller

import (
	"bytes"
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"text/template"

	"github.com/go-logr/logr"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/utils/ptr"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/builder"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/controller"
	"sigs.k8s.io/controller-runtime/pkg/controller/controllerutil"
	"sigs.k8s.io/controller-runtime/pkg/handler"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/controller-runtime/pkg/predicate"
	"sigs.k8s.io/yaml"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/Azure/dacs/cache-controller/internal/scaling"
	"github.com/Azure/dacs/cache-controller/internal/scheduling"
	"github.com/Azure/dacs/cache-controller/internal/statushelper"
	"k8s.io/client-go/tools/events"
)

// CacheReconciler reconciles a Cache object
type CacheReconciler struct {
	client.Client
	Scheme    *runtime.Scheme
	Scaler    *scaling.Reconciler
	Scheduler *scheduling.Reconciler
	Recorder  events.EventRecorder
}

// NewCacheReconciler creates a CacheReconciler, returning an error if any
// required dependency is nil.
func NewCacheReconciler(c client.Client, scheme *runtime.Scheme, recorder events.EventRecorder, scaler *scaling.Reconciler, scheduler *scheduling.Reconciler) (*CacheReconciler, error) {
	if c == nil {
		return nil, fmt.Errorf("client must not be nil")
	}
	if scheme == nil {
		return nil, fmt.Errorf("scheme must not be nil")
	}
	if recorder == nil {
		return nil, fmt.Errorf("recorder must not be nil")
	}
	if scaler == nil {
		return nil, fmt.Errorf("scaler must not be nil")
	}
	if scheduler == nil {
		return nil, fmt.Errorf("scheduler must not be nil")
	}
	return &CacheReconciler{
		Client:    c,
		Scheme:    scheme,
		Scaler:    scaler,
		Scheduler: scheduler,
		Recorder:  recorder,
	}, nil
}

const cacheFinalizerName = "storage.azure.com/finalizer"

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
// +kubebuilder:rbac:groups="",resources=pods,verbs=get;list;watch
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
			log.Info("Cache resource not found, nothing to reconcile")
			return ctrl.Result{}, nil
		}
		log.Error(err, "Failed to get Cache")
		return ctrl.Result{}, err
	}

	// Handle deletion: clean up tracker state so nodes are freed for
	// other Cache CRs, then remove the finalizer to let the API server
	// complete the delete.
	if !cache.DeletionTimestamp.IsZero() {
		if controllerutil.ContainsFinalizer(&cache, cacheFinalizerName) {
			cacheKey := req.Namespace + "/" + req.Name
			log.Info("Cache being deleted, cleaning up tracker", "cacheKey", cacheKey)
			r.Scaler.Deleted(req.NamespacedName)
			r.Scheduler.Tracker.RemoveCacheKey(cacheKey)

			patch := client.MergeFrom(cache.DeepCopy())
			controllerutil.RemoveFinalizer(&cache, cacheFinalizerName)
			if err := r.Patch(ctx, &cache, patch); err != nil {
				return ctrl.Result{}, err
			}
		}
		return ctrl.Result{}, nil
	}

	// Ensure finalizer is registered so tracker cleanup runs on deletion.
	if !controllerutil.ContainsFinalizer(&cache, cacheFinalizerName) {
		patch := client.MergeFrom(cache.DeepCopy())
		controllerutil.AddFinalizer(&cache, cacheFinalizerName)
		if err := r.Patch(ctx, &cache, patch); err != nil {
			return ctrl.Result{}, err
		}
	}

	log.V(1).Info("Reconciling Cache resource")

	// Patchhelper accumulates conditions and requeue timing.
	// Sub-conditions are initialized to Unknown by the statushelper;
	// each sub-reconciler overwrites with the actual state as it runs.
	h, err := statushelper.New(&cache, r.Client)
	if err != nil {
		return ctrl.Result{}, fmt.Errorf("creating status helper: %w", err)
	}

	reconcileErr := r.reconcileCache(ctx, &cache, h)
	if err := r.patchStatus(ctx, h, &cache, reconcileErr); err != nil {
		return ctrl.Result{}, err
	}
	return h.ToCtrlResult(), nil
}

// reconcileCache runs all sub-reconcilers (scaling, StatefulSet, Services,
// ConfigMap, scheduling). It returns the first error encountered.
func (r *CacheReconciler) reconcileCache(ctx context.Context, cache *storagev1.Cache, h *statushelper.Helper) error {
	log := logf.FromContext(ctx)

	desired, err := r.resolveNumServers(ctx, cache, h)
	if err != nil {
		return err
	}
	h.SetDesiredReplicas(desired)

	log.Info("Starting reconciliation", "desiredServers", desired)

	// Reconcile StatefulSet
	log.V(1).Info("Reconciling StatefulSet")
	if err := r.reconcileStatefulSet(ctx, cache, desired); err != nil {
		log.Error(err, "Failed to reconcile StatefulSet")
		return fmt.Errorf("failed to reconcile StatefulSet: %w", err)
	}

	// Reconcile Services
	log.V(1).Info("Reconciling Services")
	if err := r.reconcileServices(ctx, cache); err != nil {
		log.Error(err, "Failed to reconcile Services")
		return fmt.Errorf("failed to reconcile Services: %w", err)
	}

	// Reconcile ConfigMap
	log.V(1).Info("Reconciling ConfigMap")
	if err := r.reconcileConfigMap(ctx, cache, desired); err != nil {
		log.Error(err, "Failed to reconcile ConfigMap")
		return fmt.Errorf("failed to reconcile ConfigMap: %w", err)
	}

	// Schedule pods (custom scheduling, stuck-pod cleanup, excess-pod removal)
	if err := r.Scheduler.Reconcile(ctx, cache, h); err != nil {
		log.Error(err, "Failed to schedule pods")
		return err
	}

	log.V(1).Info("Reconciliation completed successfully")
	return nil
}

// patchStatus patches the Cache status with final conditions. It is always
// called after reconcileCache, regardless of whether reconciliation
// succeeded or failed. Returns reconcileErr joined with any patch error.
func (r *CacheReconciler) patchStatus(ctx context.Context, h *statushelper.Helper, cache *storagev1.Cache, reconcileErr error) error {
	log := logf.FromContext(ctx)

	r.setFinalConditions(ctx, h, cache)
	log.V(1).Info("Patching Cache status")
	if statusErr := h.Patch(ctx); statusErr != nil {
		log.Error(statusErr, "Failed to patch Cache status")
		return errors.Join(reconcileErr, statusErr)
	}
	return reconcileErr
}

// resolveNumServers determines the desired number of cache servers by
// delegating to the scaler, which handles both static and dynamic scaling.
func (r *CacheReconciler) resolveNumServers(ctx context.Context, cache *storagev1.Cache, h *statushelper.Helper) (int32, error) {
	log := logf.FromContext(ctx)

	currentReplicas, err := r.getCurrentReplicas(ctx, cache)
	if err != nil {
		return 0, err
	}

	// Capture whether stabilization was previously active so we can emit
	// events only on transitions, not on every reconciliation.
	desired, err := r.Scaler.DesiredReplicas(ctx, cache, currentReplicas, h)
	if err != nil {
		return 0, err
	}

	if desired != currentReplicas && currentReplicas > 0 {
		log.Info("Scaling cache servers", "from", currentReplicas, "to", desired)
		r.Recorder.Eventf(cache, nil, corev1.EventTypeNormal, "Scaling", "ScaleReplicas", "Scaling cache servers from %d to %d replicas", currentReplicas, desired)
	} else if desired != currentReplicas && currentReplicas == 0 {
		log.Info("Initial scaling", "desired", desired)
		r.Recorder.Eventf(cache, nil, corev1.EventTypeNormal, "Scaling", "ScaleReplicas", "Initial scale to %d replicas", desired)
	}

	return desired, nil
}

// getCurrentReplicas reads the current spec.replicas from the owned StatefulSet.
// Returns 0 if the StatefulSet doesn't exist yet (not-found). Returns an error
// for unexpected failures so the caller can distinguish "no STS" from "can't read STS".
func (r *CacheReconciler) getCurrentReplicas(ctx context.Context, cache *storagev1.Cache) (int32, error) {
	var sts appsv1.StatefulSet
	if err := r.Get(ctx, types.NamespacedName{Name: "cacheserver", Namespace: cache.Namespace}, &sts); err != nil {
		if apierrors.IsNotFound(err) {
			return 0, nil
		}
		return 0, fmt.Errorf("failed to get StatefulSet for current replicas: %w", err)
	}
	if sts.Spec.Replicas == nil {
		return 0, nil
	}
	return *sts.Spec.Replicas, nil
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
		ImagePullPolicy              string
		ServerThreads                int32
		Port                         int
		MetricsPort                  int
		EnableAccessTimeEviction     bool
		EvictionLastAccessThreshold  string
		EvictionSizeThreshold        string
		EvictionPassFrequency        string
		EvictionMaintainFreeSpace    bool
		EvictionSizeTarget           string
		InfinibandResourceLimitCount int32
		InfinibandResourceLimitName  string
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
	// Validation and defaults are applied by the admission webhook.

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
		ImagePullPolicy:              string(cfg.ImagePullPolicy),
		ServerThreads:                cache.Spec.ServerThreads,
		Port:                         int(*cache.Spec.Port),
		MetricsPort:                  int(*cache.Spec.MetricsPort),
		EnableAccessTimeEviction:     *cache.Spec.Eviction.EnableAccessTimeEviction,
		EvictionLastAccessThreshold:  *cache.Spec.Eviction.LastAccessThreshold,
		EvictionSizeThreshold:        *cache.Spec.Eviction.SizeThreshold,
		EvictionPassFrequency:        *cache.Spec.Eviction.PassFrequency,
		EvictionMaintainFreeSpace:    *cache.Spec.Eviction.EvictionMaintainFreeSpace,
		EvictionSizeTarget:           *cache.Spec.Eviction.SizeTarget,
		InfinibandResourceLimitCount: *cache.Spec.InfinibandResourceLimitCount,
		InfinibandResourceLimitName:  *cache.Spec.InfinibandResourceLimitName,
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
	// Create or Update StatefulSet. The template uses a fixed name
	// ("cacheserver"), so exactly one StatefulSet -- and therefore one
	// Cache CR -- is supported per namespace.
	// TODO(jlong): support multiple Cache CRs per namespace.
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
	switch result {
	case controllerutil.OperationResultCreated:
		log.Info("Created "+resourceType, append(baseFields, extraFields...)...)
	case controllerutil.OperationResultUpdated:
		log.Info("Updated "+resourceType, append(baseFields, extraFields...)...)
	default:
		log.V(1).Info(resourceType+" unchanged", baseFields...)
	}
}

// setFinalConditions sets ReplicasReady, server counts, and the aggregate
// Ready condition. Any condition left unset by sub-reconcilers is marked
// Unknown. Ready is True only when all sub-conditions are True; otherwise
// False. Scaling state is reported separately via status.scaling.
func (r *CacheReconciler) setFinalConditions(ctx context.Context, h *statushelper.Helper, cache *storagev1.Cache) {
	log := logf.FromContext(ctx)

	desired := h.DesiredReplicas()
	readyReplicas := r.setReplicasReady(ctx, h, cache)
	if desired != nil {
		cache.Status.DesiredServers = desired
		cache.Status.CurrentServers = &readyReplicas
		log.V(1).Info("Cache status set", "desiredServers", *desired, "readyReplicas", readyReplicas)
	}

	// Ready is True only when all sub-conditions are True; otherwise False.
	h.SetReady()
}

// setReplicasReady fetches the StatefulSet, sets the ReplicasReady condition
// on the statushelper, and returns the number of ready replicas.
func (r *CacheReconciler) setReplicasReady(ctx context.Context, h *statushelper.Helper, cache *storagev1.Cache) int32 {
	log := logf.FromContext(ctx)

	desired := h.DesiredReplicas()
	if desired == nil {
		h.MarkUnknown(storagev1.ReplicasReadyCondition, storagev1.ReasonDesiredCountUnknown, "Desired server count has not been resolved yet")
		return 0
	}

	var sts appsv1.StatefulSet
	err := r.Get(ctx, types.NamespacedName{Name: "cacheserver", Namespace: cache.Namespace}, &sts)

	if apierrors.IsNotFound(err) {
		log.V(1).Info("StatefulSet not found, will be created")
		h.MarkFalse(storagev1.ReplicasReadyCondition, storagev1.ReasonStatefulSetNotFound, "Cache server StatefulSet does not exist yet")
		return 0
	}
	if err != nil {
		log.Error(err, "Failed to get StatefulSet for status update", "statefulSetName", "cacheserver")
		h.MarkUnknown(storagev1.ReplicasReadyCondition, storagev1.ReasonStatefulSetGetError, "Failed to get StatefulSet: %v", err)
		return 0
	}

	ready := sts.Status.ReadyReplicas
	log.V(1).Info("StatefulSet status",
		"readyReplicas", ready,
		"desiredReplicas", *desired,
		"updatedReplicas", sts.Status.UpdatedReplicas,
		"currentReplicas", sts.Status.CurrentReplicas)

	switch {
	case ready == *desired:
		h.MarkTrue(storagev1.ReplicasReadyCondition, storagev1.ReasonAllReplicasReady, "All %d cache servers are ready", *desired)
	case ready > 0:
		h.MarkFalse(storagev1.ReplicasReadyCondition, storagev1.ReasonSomeReplicasNotReady, "%d/%d cache servers are ready", ready, *desired)
	default:
		h.MarkFalse(storagev1.ReplicasReadyCondition, storagev1.ReasonNoReplicasReady, "No cache servers are ready yet")
	}

	return ready
}

// SetupWithManager sets up the controller with the Manager.
func (r *CacheReconciler) SetupWithManager(ctx context.Context, mgr ctrl.Manager) error {
	// Register field indexes needed for scaling node lookups.
	if err := r.Scaler.RegisterIndexes(ctx, mgr); err != nil {
		return err
	}

	return ctrl.NewControllerManagedBy(mgr).
		WithOptions(controller.Options{
			RecoverPanic: ptr.To(true),
		}).
		For(&storagev1.Cache{},
			builder.WithPredicates(
				predicate.GenerationChangedPredicate{},
			),
		).
		Owns(&appsv1.StatefulSet{},
			builder.WithPredicates(predicate.Or(
				predicate.GenerationChangedPredicate{},
				predicate.LabelChangedPredicate{},
				stsStatusChangedPredicate{},
			)),
		).
		Owns(&corev1.Service{},
			builder.WithPredicates(predicate.Or(
				predicate.GenerationChangedPredicate{},
				predicate.LabelChangedPredicate{},
			)),
		).
		Owns(&corev1.ConfigMap{},
			builder.WithPredicates(predicate.Or(
				predicate.GenerationChangedPredicate{},
				predicate.LabelChangedPredicate{},
			)),
		).
		Watches(&corev1.Node{},
			handler.EnqueueRequestsFromMapFunc(r.Scaler.MapNodeToCaches),
			builder.WithPredicates(NodePredicate{}),
		).
		Watches(&corev1.Pod{},
			handler.EnqueueRequestsFromMapFunc(r.Scheduler.MapPodToCaches),
			builder.WithPredicates(cacheserverPodPredicate{}),
		).
		Named("cache").
		Complete(r)
}

//go:embed templates/*.yaml
var templateFS embed.FS
