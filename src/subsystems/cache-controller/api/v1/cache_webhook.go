/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

import (
	"context"
	"fmt"
	"regexp"
	"strconv"
	"strings"

	apierrors "k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/runtime/schema"
	"k8s.io/apimachinery/pkg/util/validation/field"
	"k8s.io/utils/ptr"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/controller-runtime/pkg/webhook"
	"sigs.k8s.io/controller-runtime/pkg/webhook/admission"
)

var durationRegex = regexp.MustCompile(`^(\d+d)?(\d+h)?(\d+m)?(\d+s)?$`)

func (r *Cache) SetupWebhookWithManager(mgr ctrl.Manager) error {
	return ctrl.NewWebhookManagedBy(mgr).
		For(r).
		WithValidator(r).
		WithDefaulter(r).
		Complete()
}

// +kubebuilder:webhook:path=/mutate-storage-azure-com-v1-cache,mutating=true,failurePolicy=fail,sideEffects=None,groups=storage.azure.com,resources=caches,verbs=create;update,versions=v1,name=mutate.cache.storage.azure.com,admissionReviewVersions=v1

var _ webhook.CustomDefaulter = &Cache{}

func (r *Cache) Default(ctx context.Context, obj runtime.Object) error {
	cache, ok := obj.(*Cache)
	if !ok {
		return fmt.Errorf("expected a Cache object but got %T", obj)
	}
	log := log.FromContext(ctx).WithName("Default").WithValues("name", cache.Name, "namespace", cache.Namespace)
	log.V(2).Info("defaulting Cache")

	if cache.Spec.Port == nil {
		cache.Spec.Port = ptr.To(DefaultPort)
	}
	if cache.Spec.MetricsPort == nil {
		cache.Spec.MetricsPort = ptr.To(DefaultMetricsPort)
	}
	if cache.Spec.NodeSelectorKey == nil {
		cache.Spec.NodeSelectorKey = ptr.To(DefaultNodeSelectorKey)
	}
	if cache.Spec.NodeSelectorValue == nil {
		cache.Spec.NodeSelectorValue = ptr.To(DefaultNodeSelectorValue)
	}
	if cache.Spec.InfinibandResourceLimitName == nil {
		cache.Spec.InfinibandResourceLimitName = ptr.To(DefaultInfinibandResourceLimitName)
	}
	if cache.Spec.InfinibandResourceLimitCount == nil {
		cache.Spec.InfinibandResourceLimitCount = ptr.To(DefaultInfinibandResourceLimitCount)
	}

	// Default scaling configuration (early for validation order)
	if cache.Spec.Scaling.Static == nil && cache.Spec.Scaling.Dynamic == nil {
		cache.Spec.Scaling.Static = &StaticScaleConfig{NumServers: DefaultNumServers}
	}

	if cache.Spec.Eviction == nil {
		cache.Spec.Eviction = &EvictionConfig{}
	}
	if cache.Spec.Eviction.EnableAccessTimeEviction == nil {
		cache.Spec.Eviction.EnableAccessTimeEviction = ptr.To(DefaultEnableAccessTimeEviction)
	}
	if cache.Spec.Eviction.EvictionMaintainFreeSpace == nil {
		cache.Spec.Eviction.EvictionMaintainFreeSpace = ptr.To(DefaultEvictionMaintainFreeSpace)
	}
	if cache.Spec.Eviction.LastAccessThreshold == nil {
		cache.Spec.Eviction.LastAccessThreshold = ptr.To(DefaultLastAccessThreshold)
	}
	if cache.Spec.Eviction.SizeThreshold == nil {
		cache.Spec.Eviction.SizeThreshold = ptr.To(DefaultSizeThreshold)
	}
	if cache.Spec.Eviction.PassFrequency == nil {
		cache.Spec.Eviction.PassFrequency = ptr.To(DefaultPassFrequency)
	}
	if cache.Spec.Eviction.SizeTarget == nil {
		cache.Spec.Eviction.SizeTarget = ptr.To(DefaultSizeTarget)
	}

	if cache.Spec.Storage == nil {
		cache.Spec.Storage = &StorageConfig{}
	}

	if cache.Spec.Storage.Ephemeral == nil {
		cache.Spec.Storage.Ephemeral = ptr.To(false)
	}

	if !*cache.Spec.Storage.Ephemeral {
		if cache.Spec.Storage.HostPath == nil {
			cache.Spec.Storage.HostPath = &HostPathConfig{}
		}
		if cache.Spec.Storage.HostPath.NodeCacheFolderPath == "" {
			cache.Spec.Storage.HostPath.NodeCacheFolderPath = DefaultNodeCacheFolderPath
		}
	}
	return nil
}

// +kubebuilder:webhook:path=/validate-storage-azure-com-v1-cache,mutating=false,failurePolicy=fail,sideEffects=None,groups=storage.azure.com,resources=caches,verbs=create;update,versions=v1,name=validate.cache.storage.azure.com,admissionReviewVersions=v1

var _ webhook.CustomValidator = &Cache{}

func (r *Cache) ValidateCreate(ctx context.Context, obj runtime.Object) (admission.Warnings, error) {
	cache, ok := obj.(*Cache)
	if !ok {
		return nil, fmt.Errorf("expected a Cache object but got %T", obj)
	}
	log := log.FromContext(ctx).WithName("ValidateCreate").WithValues("name", cache.Name, "namespace", cache.Namespace)
	log.V(2).Info("validating Cache create")
	return cache.validateCache()
}

func (r *Cache) ValidateUpdate(ctx context.Context, oldObj, newObj runtime.Object) (admission.Warnings, error) {
	cache, ok := newObj.(*Cache)
	if !ok {
		return nil, fmt.Errorf("expected a Cache object but got %T", newObj)
	}
	log := log.FromContext(ctx).WithName("ValidateUpdate").WithValues("name", cache.Name, "namespace", cache.Namespace)
	log.V(2).Info("validating Cache update")
	return cache.validateCache()
}

func (r *Cache) ValidateDelete(ctx context.Context, obj runtime.Object) (admission.Warnings, error) {
	return nil, nil
}

func (r *Cache) validateCache() (admission.Warnings, error) {
	var allErrs field.ErrorList
	var warnings admission.Warnings

	// Validate required fields
	specPath := field.NewPath("spec")
	if r.Spec.ServerSizeInGB < 1 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("serverSizeInGB"), r.Spec.ServerSizeInGB, "must be at least 1"))
	}
	if r.Spec.ServerThreads < 4 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("serverThreads"), r.Spec.ServerThreads, "must be at least 4"))
	}

	// Validate optional resource limits
	if r.Spec.CpuLimit != nil && *r.Spec.CpuLimit < 1 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("cpuLimit"), *r.Spec.CpuLimit, "must be at least 1"))
	}
	if r.Spec.MemoryLimitMB != nil && *r.Spec.MemoryLimitMB < 1024 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("memoryLimitMB"), *r.Spec.MemoryLimitMB, "must be at least 1024"))
	}
	if r.Spec.EgressBandwidthLimitGbps != nil && *r.Spec.EgressBandwidthLimitGbps < 1 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("egressBandwidthLimitGbps"), *r.Spec.EgressBandwidthLimitGbps, "must be at least 1"))
	}
	if r.Spec.InfinibandResourceLimitCount != nil && *r.Spec.InfinibandResourceLimitCount < 0 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("infinibandResourceLimitCount"), *r.Spec.InfinibandResourceLimitCount, "must be at least 0"))
	}
	if r.Spec.InfinibandResourceLimitCount != nil && *r.Spec.InfinibandResourceLimitCount > 0 {
		if r.Spec.InfinibandResourceLimitName == nil || *r.Spec.InfinibandResourceLimitName == "" {
			allErrs = append(allErrs, field.Required(specPath.Child("infinibandResourceLimitName"), "required when infinibandResourceLimitCount > 0"))
		}
	}

	// Validate port numbers
	if r.Spec.Port != nil && (*r.Spec.Port < 1 || *r.Spec.Port > 65535) {
		allErrs = append(allErrs, field.Invalid(specPath.Child("port"), *r.Spec.Port, "must be between 1 and 65535"))
	}
	if r.Spec.MetricsPort != nil && (*r.Spec.MetricsPort < 1 || *r.Spec.MetricsPort > 65535) {
		allErrs = append(allErrs, field.Invalid(specPath.Child("metricsPort"), *r.Spec.MetricsPort, "must be between 1 and 65535"))
	}

	// Validate scaling configuration
	scalingPath := field.NewPath("spec", "scaling")
	if r.Spec.Scaling.Static == nil && r.Spec.Scaling.Dynamic == nil {
		allErrs = append(allErrs, field.Required(scalingPath, "must specify either static or dynamic scaling"))
	}
	if r.Spec.Scaling.Static != nil && r.Spec.Scaling.Dynamic != nil {
		allErrs = append(allErrs, field.Invalid(scalingPath, r.Spec.Scaling, "cannot specify both static and dynamic scaling; choose one"))
	}

	if r.Spec.Scaling.Static != nil {
		staticPath := scalingPath.Child("static")
		if r.Spec.Scaling.Static.NumServers < 1 {
			allErrs = append(allErrs, field.Invalid(staticPath.Child("numServers"), r.Spec.Scaling.Static.NumServers, "must be at least 1"))
		}
	}

	if r.Spec.Scaling.Dynamic != nil {
		dynamicPath := scalingPath.Child("dynamic")
		if r.Spec.Scaling.Dynamic.PercentageOfNodesToUse < 1 || r.Spec.Scaling.Dynamic.PercentageOfNodesToUse > 100 {
			allErrs = append(allErrs, field.Invalid(dynamicPath.Child("percentageOfNodesToUse"), r.Spec.Scaling.Dynamic.PercentageOfNodesToUse, "must be between 1 and 100"))
		}
		if r.Spec.Scaling.Dynamic.MinServers != nil {
			if *r.Spec.Scaling.Dynamic.MinServers < 1 || *r.Spec.Scaling.Dynamic.MinServers > 99 {
				allErrs = append(allErrs, field.Invalid(dynamicPath.Child("minServers"), *r.Spec.Scaling.Dynamic.MinServers, "must be between 1 and 99"))
			}
		}
		if r.Spec.Scaling.Dynamic.MaxServers != nil {
			if *r.Spec.Scaling.Dynamic.MaxServers < 1 || *r.Spec.Scaling.Dynamic.MaxServers > 99 {
				allErrs = append(allErrs, field.Invalid(dynamicPath.Child("maxServers"), *r.Spec.Scaling.Dynamic.MaxServers, "must be between 1 and 99"))
			}
		}
		if r.Spec.Scaling.Dynamic.MinServers != nil && r.Spec.Scaling.Dynamic.MaxServers != nil {
			if *r.Spec.Scaling.Dynamic.MinServers > *r.Spec.Scaling.Dynamic.MaxServers {
				allErrs = append(allErrs, field.Invalid(dynamicPath.Child("minServers"), *r.Spec.Scaling.Dynamic.MinServers, fmt.Sprintf("minServers (%d) cannot exceed maxServers (%d)", *r.Spec.Scaling.Dynamic.MinServers, *r.Spec.Scaling.Dynamic.MaxServers)))
			}
		}
	}

	if r.Spec.Storage != nil {
		storagePath := field.NewPath("spec", "storage")
		if r.Spec.Storage.Ephemeral != nil && *r.Spec.Storage.Ephemeral && r.Spec.Storage.HostPath != nil {
			allErrs = append(allErrs, field.Invalid(storagePath, r.Spec.Storage, "cannot specify both ephemeral and hostPath storage; choose one"))
		}
		if r.Spec.Storage.HostPath != nil && r.Spec.Storage.HostPath.NodeCacheFolderPath == "" {
			allErrs = append(allErrs, field.Required(storagePath.Child("hostPath", "nodeCacheFolderPath"), "nodeCacheFolderPath is required when using hostPath storage"))
		}
	}

	if r.Spec.Eviction != nil {
		evictionPath := field.NewPath("spec", "eviction")
		if r.Spec.Eviction.LastAccessThreshold != nil {
			if err := validateDuration(*r.Spec.Eviction.LastAccessThreshold); err != nil {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("lastAccessThreshold"), *r.Spec.Eviction.LastAccessThreshold, fmt.Sprintf("invalid duration format: %v (expected format like '30d', '1h30m', '30d12h41m29s')", err)))
			}
		}
		if r.Spec.Eviction.PassFrequency != nil {
			if err := validateDuration(*r.Spec.Eviction.PassFrequency); err != nil {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("passFrequency"), *r.Spec.Eviction.PassFrequency, fmt.Sprintf("invalid duration format: %v (expected format like '1h', '30m', '1h30m')", err)))
			}
		}
		if r.Spec.Eviction.SizeThreshold != nil {
			if err := validatePercentage(*r.Spec.Eviction.SizeThreshold, "sizeThreshold"); err != nil {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("sizeThreshold"), *r.Spec.Eviction.SizeThreshold, err.Error()))
			}
		}
		if r.Spec.Eviction.SizeTarget != nil {
			if err := validatePercentage(*r.Spec.Eviction.SizeTarget, "sizeTarget"); err != nil {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("sizeTarget"), *r.Spec.Eviction.SizeTarget, err.Error()))
			}
		}
		// Validate sizeTarget < sizeThreshold (target must be lower than trigger threshold)
		if r.Spec.Eviction.SizeThreshold != nil && r.Spec.Eviction.SizeTarget != nil {
			threshold, errT := strconv.Atoi(*r.Spec.Eviction.SizeThreshold)
			target, errG := strconv.Atoi(*r.Spec.Eviction.SizeTarget)
			if errT == nil && errG == nil && target >= threshold {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("sizeTarget"), *r.Spec.Eviction.SizeTarget,
					fmt.Sprintf("sizeTarget (%d%%) must be less than sizeThreshold (%d%%)", target, threshold)))
			}
		}
	}

	if r.Spec.Port != nil && r.Spec.MetricsPort != nil && *r.Spec.Port == *r.Spec.MetricsPort {
		allErrs = append(allErrs, field.Invalid(field.NewPath("spec", "metricsPort"), *r.Spec.MetricsPort, fmt.Sprintf("metricsPort (%d) must be different from port (%d)", *r.Spec.MetricsPort, *r.Spec.Port)))
	}

	if len(allErrs) > 0 {
		return warnings, apierrors.NewInvalid(schema.GroupKind{Group: "storage.azure.com", Kind: "Cache"}, r.Name, allErrs)
	}
	return warnings, nil
}

func validateDuration(s string) error {
	if !durationRegex.MatchString(s) || s == "" {
		return fmt.Errorf("must match pattern like '30d', '1h30m', '30d12h41m29s'")
	}
	return nil
}

func validatePercentage(s string, fieldName string) error {

	trimmed := strings.TrimSpace(s)
	if trimmed != s {
		return fmt.Errorf("%s must not contain leading or trailing whitespace", fieldName)
	}
	value, err := strconv.Atoi(s)
	if err != nil {
		return fmt.Errorf("%s must be a numeric string representing a percentage (0-100)", fieldName)
	}
	if value < 0 || value > 100 {
		return fmt.Errorf("%s must be between 0 and 100, got %d", fieldName, value)
	}
	return nil
}
