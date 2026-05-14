package controller

/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

import (
	"k8s.io/apimachinery/pkg/labels"
)

const (
	// CacheNameLabelKey is the label key added to resources to identify the owning Cache CR.
	CacheNameLabelKey = "dacs.azure.com/cache-name"

	// cacheServerAppLabel is the constant app label shared by all cache server resources.
	cacheServerAppLabel = "cacheserver"
)

// CacheServerSelector matches resources with app=cacheserver.
// Used to scope the controller manager's informer caches to only
// watch cache-related resources.
var CacheServerSelector = labels.SelectorFromSet(labels.Set{"app": cacheServerAppLabel})
