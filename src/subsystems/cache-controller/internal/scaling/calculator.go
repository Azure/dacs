/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package scaling

import (
	"fmt"
	"math"
)

// CalculateDesiredReplicas computes the target replica count for a dynamically scaled cache.
//
// Parameters:
//
//	nodeCount:         total schedulable nodes matching the cache's node selector
//	percentageOfNodes: from DynamicScaleConfig.PercentageOfNodesToUse (1-100)
//	minServers:        optional floor (nil = no floor beyond 1)
//	maxServers:        optional ceiling (nil = no ceiling)
//
// Returns the clamped replica count (always >= 1) or an error for invalid inputs.
func CalculateDesiredReplicas(nodeCount int, percentageOfNodes int32, minServers, maxServers *int32) (int32, error) {
	if nodeCount < 0 {
		return 0, fmt.Errorf("invalid node count: %d", nodeCount)
	}
	if percentageOfNodes <= 0 || percentageOfNodes > 100 {
		return 0, fmt.Errorf("invalid percentage of nodes: %d (must be 1-100)", percentageOfNodes)
	}
	if minServers != nil && *minServers < 1 {
		return 0, fmt.Errorf("invalid min servers: %d (must be >= 1)", *minServers)
	}
	if maxServers != nil && *maxServers < 1 {
		return 0, fmt.Errorf("invalid max servers: %d (must be >= 1)", *maxServers)
	}
	if minServers != nil && maxServers != nil && *minServers > *maxServers {
		return 0, fmt.Errorf("min servers (%d) exceeds max servers (%d)", *minServers, *maxServers)
	}

	// Compute raw value using ceiling so we always round up
	raw := int32(math.Ceil(float64(nodeCount) * float64(percentageOfNodes) / 100.0))
	raw = max(raw, 1)

	if minServers != nil {
		raw = max(raw, *minServers)
	}
	if maxServers != nil {
		raw = min(raw, *maxServers)
	}

	return raw, nil
}
