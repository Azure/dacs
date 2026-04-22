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
//	minServers:        if set and the calculated count is below this threshold (but > 0),
//	                   the result is floored to 0 - the cache will not run until enough
//	                   nodes are available.
//	maxServers:        if set and the calculated count exceeds this value, the result is capped.
//
// Returns the replica count using floor rounding, or an error for invalid inputs.
func CalculateDesiredReplicas(nodeCount int, percentageOfNodes int32, minServers, maxServers *int32) (int32, error) {
	if nodeCount < 0 || nodeCount > math.MaxInt32 {
		return 0, fmt.Errorf("invalid node count: %d", nodeCount)
	}
	if percentageOfNodes < 1 || percentageOfNodes > 100 {
		return 0, fmt.Errorf("invalid percentage of nodes: %d (must be 1-100)", percentageOfNodes)
	}

	// Safe integer arithmetic: int64 intermediate cannot overflow because
	// nodeCount <= MaxInt32 and percentageOfNodes <= 100, so the product
	// fits well within int64. The quotient is <= nodeCount, so it fits in int32.
	desired := int32(int64(nodeCount) * int64(percentageOfNodes) / 100)

	if minServers != nil && desired > 0 && desired < *minServers {
		return 0, nil
	}
	if maxServers != nil {
		desired = min(desired, *maxServers)
	}
	return desired, nil
}
