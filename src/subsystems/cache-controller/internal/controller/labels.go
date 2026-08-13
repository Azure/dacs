package controller

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
