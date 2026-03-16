package controller

/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

import (
	"k8s.io/apimachinery/pkg/labels"
)

const (
	// CacheNameLabelKey is the label key added to StatefulSets to identify the owning Cache CR.
	CacheNameLabelKey = "dacs.azure.com/cache-name"
)

var (
	// CacheServerSelector is a compiled labels.Selector for use with client.MatchingLabelsSelector
	// and manager cache filtering.
	CacheServerSelector = labels.SelectorFromSet(map[string]string{"app": "cacheserver"})
)
