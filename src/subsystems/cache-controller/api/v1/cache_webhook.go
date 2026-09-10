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

package v1

import (
	"context"
	"fmt"
	"strconv"
	"strings"

	apierrors "k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/runtime/schema"
	"k8s.io/apimachinery/pkg/util/validation"
	"k8s.io/apimachinery/pkg/util/validation/field"
	"k8s.io/utils/ptr"
	ctrl "sigs.k8s.io/controller-runtime"
	logf "sigs.k8s.io/controller-runtime/pkg/log"
	"sigs.k8s.io/controller-runtime/pkg/webhook/admission"
)

func (r *Cache) SetupWebhookWithManager(mgr ctrl.Manager) error {
	return ctrl.NewWebhookManagedBy(mgr, &Cache{}).
		WithValidator(r).
		WithDefaulter(r).
		Complete()
}

// +kubebuilder:webhook:path=/mutate-storage-azure-com-v1-cache,mutating=true,failurePolicy=fail,sideEffects=None,groups=storage.azure.com,resources=caches,verbs=create;update,versions=v1,name=mutate.cache.storage.azure.com,admissionReviewVersions=v1

var _ admission.Defaulter[*Cache] = &Cache{}

func (r *Cache) Default(ctx context.Context, cache *Cache) error {
	if cache == nil {
		return fmt.Errorf("cache object is nil")
	}

	log := logf.FromContext(ctx).WithName("Default").WithValues("name", cache.Name, "namespace", cache.Namespace)
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
	if cache.Spec.Eviction.BucketInterval == nil {
		cache.Spec.Eviction.BucketInterval = ptr.To(DefaultBucketInterval)
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

var _ admission.Validator[*Cache] = &Cache{}

func (r *Cache) ValidateCreate(ctx context.Context, cache *Cache) (admission.Warnings, error) {
	if cache == nil {
		return nil, fmt.Errorf("cache object is nil")
	}
	log := logf.FromContext(ctx).WithName("ValidateCreate").WithValues("name", cache.Name, "namespace", cache.Namespace)
	log.V(2).Info("validating Cache create")
	return cache.validateCache()
}

func (r *Cache) ValidateUpdate(ctx context.Context, oldCache, newCache *Cache) (admission.Warnings, error) {
	if newCache == nil {
		return nil, fmt.Errorf("new cache object is nil")
	}
	log := logf.FromContext(ctx).WithName("ValidateUpdate").WithValues("name", newCache.Name, "namespace", newCache.Namespace)
	log.V(2).Info("validating Cache update")
	return newCache.validateCache()
}

func (r *Cache) ValidateDelete(ctx context.Context, cache *Cache) (admission.Warnings, error) {
	return nil, nil
}

func (r *Cache) validateCache() (admission.Warnings, error) { //nolint:gocyclo
	var allErrs field.ErrorList
	var warnings admission.Warnings

	// Validate metadata.name (or metadata.generateName) for derived resource
	// naming constraints. The controller creates Services and ConfigMaps with
	// suffixed names (e.g. "{name}-discovery"), which must be valid DNS labels
	// (RFC 1123).
	//
	// generateName is supported: the apiserver runs name generation between
	// the mutating and validating admission webhooks, so by the time we get
	// here r.Name is already populated with "{generateName}{5-char-suffix}".
	// We still validate r.GenerateName explicitly so users get an error that
	// references the field they actually set.
	namePath := field.NewPath("metadata", "name")
	genNamePath := field.NewPath("metadata", "generateName")

	switch {
	case r.Name == "" && r.GenerateName == "":
		allErrs = append(allErrs, field.Required(namePath,
			"must specify metadata.name or metadata.generateName"))
	case r.GenerateName != "":
		if len(r.GenerateName) > MaxCacheGenerateNameLength {
			allErrs = append(allErrs, field.Invalid(genNamePath, r.GenerateName,
				fmt.Sprintf("must be at most %d characters; the apiserver appends a %d-char random suffix and the controller appends suffixes up to %d characters to create Service names, which are limited to 63",
					MaxCacheGenerateNameLength, RandomNameSuffixLength, len(DiscoverySuffix))))
		}
		// Validate the generated name (generateName + 5 random lowercase
		// alphanumeric chars) as a DNS-1123 label. Appending a placeholder
		// suffix of valid characters lets a generateName ending in '-' pass
		// (the random suffix would prevent a trailing hyphen in the final name).
		candidate := r.GenerateName + strings.Repeat("a", RandomNameSuffixLength)
		for _, msg := range validation.IsDNS1123Label(candidate) {
			allErrs = append(allErrs, field.Invalid(genNamePath, r.GenerateName, msg))
		}
	case r.Name != "":
		if len(r.Name) > MaxCacheNameLength {
			allErrs = append(allErrs, field.Invalid(namePath, r.Name,
				fmt.Sprintf("must be at most %d characters; the controller appends suffixes up to %d characters to create Service names, which are limited to 63", MaxCacheNameLength, len(DiscoverySuffix))))
		}
		for _, msg := range validation.IsDNS1123Label(r.Name) {
			allErrs = append(allErrs, field.Invalid(namePath, r.Name, msg))
		}
		// Names ending in "-discovery" are rejected because the controller
		// derives a discovery Service named "{cacheName}-discovery" from
		// every Cache. A Cache named "{x}-discovery" would have a main
		// Service whose name collides with the discovery Service derived
		// from a Cache named "{x}".
		if strings.HasSuffix(r.Name, DiscoverySuffix) {
			allErrs = append(allErrs, field.Invalid(namePath, r.Name,
				fmt.Sprintf("must not end with %q; that suffix is reserved for the discovery Service derived from another Cache and would cause a Service name collision", DiscoverySuffix)))
		}
	}

	specPath := field.NewPath("spec")

	// Required fields (set by the defaulter, but validated here to reject
	// any object that somehow bypasses defaulting).
	if r.Spec.Port == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("port"), "must be set"))
	}
	if r.Spec.MetricsPort == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("metricsPort"), "must be set"))
	}
	if r.Spec.NodeSelectorKey == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("nodeSelectorKey"), "must be set"))
	}
	if r.Spec.NodeSelectorValue == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("nodeSelectorValue"), "must be set"))
	}
	if r.Spec.Storage == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("storage"), "must be set"))
	}
	if r.Spec.Eviction == nil {
		allErrs = append(allErrs, field.Required(specPath.Child("eviction"), "must be set"))
	} else {
		evictionPath := specPath.Child("eviction")
		if r.Spec.Eviction.EnableAccessTimeEviction == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("enableAccessTimeEviction"), "must be set"))
		}
		if r.Spec.Eviction.EvictionMaintainFreeSpace == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("evictionMaintainFreeSpace"), "must be set"))
		}
		if r.Spec.Eviction.LastAccessThreshold == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("lastAccessThreshold"), "must be set"))
		}
		if r.Spec.Eviction.SizeThreshold == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("sizeThreshold"), "must be set"))
		}
		if r.Spec.Eviction.PassFrequency == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("passFrequency"), "must be set"))
		}
		if r.Spec.Eviction.SizeTarget == nil {
			allErrs = append(allErrs, field.Required(evictionPath.Child("sizeTarget"), "must be set"))
		}
	}

	// Value-range validation
	if r.Spec.ServerSizeInGB < 1 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("serverSizeInGB"), r.Spec.ServerSizeInGB, "must be at least 1"))
	}
	if r.Spec.ServerThreads < 4 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("serverThreads"), r.Spec.ServerThreads, "must be at least 4"))
	}

	// Validate optional resource limits
	if r.Spec.CpuLimit != nil && *r.Spec.CpuLimit < 0 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("cpuLimit"), *r.Spec.CpuLimit, "must be at least 0"))
	}
	if r.Spec.MemoryLimitMB != nil && *r.Spec.MemoryLimitMB != 0 && *r.Spec.MemoryLimitMB < 1024 {
		allErrs = append(allErrs, field.Invalid(specPath.Child("memoryLimitMB"), *r.Spec.MemoryLimitMB, "must be 0 or at least 1024"))
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

	// Reject values that could inject structure when rendered into the workload
	// template (YAML node affinity, resource map keys, hostPath, and shell
	// command text).
	if r.Spec.NodeSelectorKey != nil {
		if err := ValidateNodeSelectorKey(*r.Spec.NodeSelectorKey); err != nil {
			allErrs = append(allErrs, field.Invalid(specPath.Child("nodeSelectorKey"), *r.Spec.NodeSelectorKey, err.Error()))
		}
	}
	if r.Spec.NodeSelectorValue != nil {
		if err := ValidateNodeSelectorValue(*r.Spec.NodeSelectorValue); err != nil {
			allErrs = append(allErrs, field.Invalid(specPath.Child("nodeSelectorValue"), *r.Spec.NodeSelectorValue, err.Error()))
		}
	}
	if r.Spec.InfinibandResourceLimitName != nil && *r.Spec.InfinibandResourceLimitName != "" {
		if err := ValidateInfinibandResourceName(*r.Spec.InfinibandResourceLimitName); err != nil {
			allErrs = append(allErrs, field.Invalid(specPath.Child("infinibandResourceLimitName"), *r.Spec.InfinibandResourceLimitName, err.Error()))
		}
	}
	if r.Spec.Storage != nil && r.Spec.Storage.HostPath != nil && r.Spec.Storage.HostPath.NodeCacheFolderPath != "" {
		if err := ValidateNodeCacheFolderPath(r.Spec.Storage.HostPath.NodeCacheFolderPath); err != nil {
			allErrs = append(allErrs, field.Invalid(field.NewPath("spec", "storage", "hostPath", "nodeCacheFolderPath"), r.Spec.Storage.HostPath.NodeCacheFolderPath, err.Error()))
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
		if r.Spec.Scaling.Static.NumServers > 499 {
			allErrs = append(allErrs, field.Invalid(staticPath.Child("numServers"), r.Spec.Scaling.Static.NumServers, "must be at most 499"))
		}
	}

	if r.Spec.Scaling.Dynamic != nil {
		dynamicPath := scalingPath.Child("dynamic")
		if r.Spec.Scaling.Dynamic.PercentageOfNodesToUse < 1 || r.Spec.Scaling.Dynamic.PercentageOfNodesToUse > 100 {
			allErrs = append(allErrs, field.Invalid(dynamicPath.Child("percentageOfNodesToUse"), r.Spec.Scaling.Dynamic.PercentageOfNodesToUse, "must be between 1 and 100"))
		}
		if r.Spec.Scaling.Dynamic.MinServers != nil {
			if *r.Spec.Scaling.Dynamic.MinServers < 1 || *r.Spec.Scaling.Dynamic.MinServers > 499 {
				allErrs = append(allErrs, field.Invalid(dynamicPath.Child("minServers"), *r.Spec.Scaling.Dynamic.MinServers, "must be between 1 and 499"))
			}
		}
		if r.Spec.Scaling.Dynamic.MaxServers != nil {
			if *r.Spec.Scaling.Dynamic.MaxServers < 1 || *r.Spec.Scaling.Dynamic.MaxServers > 499 {
				allErrs = append(allErrs, field.Invalid(dynamicPath.Child("maxServers"), *r.Spec.Scaling.Dynamic.MaxServers, "must be between 1 and 499"))
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
		if r.Spec.Eviction.BucketInterval != nil {
			if err := ValidateBucketInterval(*r.Spec.Eviction.BucketInterval); err != nil {
				allErrs = append(allErrs, field.Invalid(evictionPath.Child("bucketInterval"), *r.Spec.Eviction.BucketInterval, fmt.Sprintf("invalid bucket interval: %v (expected a duration like '15m', '1h', at most 1 week)", err)))
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
	return ValidateDuration(s)
}

func validatePercentage(s string, fieldName string) error {
	return ValidatePercentage(s, fieldName)
}
