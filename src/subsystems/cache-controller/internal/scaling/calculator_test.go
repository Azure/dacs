/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package scaling

import (
	"math"
	"testing"

	"k8s.io/utils/ptr"
)

func TestCalculateDesiredReplicas(t *testing.T) {
	tests := []struct {
		name              string
		nodeCount         int
		percentageOfNodes int32
		minServers        *int32
		maxServers        *int32
		expected          int32
		expectErr         bool
	}{
		{
			name:              "basic 50% of 10 nodes",
			nodeCount:         10,
			percentageOfNodes: 50,
			expected:          5,
		},
		{
			name:              "zero nodes returns 0",
			nodeCount:         0,
			percentageOfNodes: 50,
			expected:          0,
		},
		{
			name:              "floors down: 50% of 3 nodes = floor(1.5) = 1",
			nodeCount:         3,
			percentageOfNodes: 50,
			expected:          1,
		},
		{
			name:              "100% of 5 nodes",
			nodeCount:         5,
			percentageOfNodes: 100,
			expected:          5,
		},
		{
			name:              "10% of 1 node = floor(0.1) = 0",
			nodeCount:         1,
			percentageOfNodes: 10,
			expected:          0,
		},
		{
			name:              "25% of 7 nodes = floor(1.75) = 1",
			nodeCount:         7,
			percentageOfNodes: 25,
			expected:          1,
		},
		{
			name:              "zero percentage returns error",
			nodeCount:         10,
			percentageOfNodes: 0,
			expectErr:         true,
		},
		{
			name:              "single node 100% returns 1",
			nodeCount:         1,
			percentageOfNodes: 100,
			expected:          1,
		},
		{
			name:              "large cluster: 30% of 100 nodes = 30",
			nodeCount:         100,
			percentageOfNodes: 30,
			expected:          30,
		},
		{
			name:              "33% of 10 nodes = floor(3.3) = 3",
			nodeCount:         10,
			percentageOfNodes: 33,
			expected:          3,
		},
		// Error cases
		{
			name:              "negative node count returns error",
			nodeCount:         -5,
			percentageOfNodes: 50,
			expectErr:         true,
		},
		{
			name:              "negative percentage returns error",
			nodeCount:         10,
			percentageOfNodes: -1,
			expectErr:         true,
		},
		{
			name:              "percentage over 100 returns error",
			nodeCount:         10,
			percentageOfNodes: 150,
			expectErr:         true,
		},
		// Server bounds: max only
		{
			name:              "max only, below max passes through",
			nodeCount:         10,
			percentageOfNodes: 50,
			maxServers:        ptr.To[int32](10),
			expected:          5,
		},
		{
			name:              "max only, at max passes through",
			nodeCount:         10,
			percentageOfNodes: 100,
			maxServers:        ptr.To[int32](10),
			expected:          10,
		},
		{
			name:              "max only, above max caps",
			nodeCount:         100,
			percentageOfNodes: 50,
			maxServers:        ptr.To[int32](10),
			expected:          10,
		},
		// Server bounds: min only
		{
			name:              "min only, above min passes through",
			nodeCount:         10,
			percentageOfNodes: 50,
			minServers:        ptr.To[int32](3),
			expected:          5,
		},
		{
			name:              "min only, at min passes through",
			nodeCount:         10,
			percentageOfNodes: 30,
			minServers:        ptr.To[int32](3),
			expected:          3,
		},
		{
			name:              "min only, below min floors to zero",
			nodeCount:         10,
			percentageOfNodes: 20,
			minServers:        ptr.To[int32](3),
			expected:          0,
		},
		{
			name:              "min only, rawDesired zero stays zero",
			nodeCount:         0,
			percentageOfNodes: 50,
			minServers:        ptr.To[int32](3),
			expected:          0,
		},
		// Server bounds: both
		{
			name:              "both, in range",
			nodeCount:         10,
			percentageOfNodes: 50,
			minServers:        ptr.To[int32](3),
			maxServers:        ptr.To[int32](10),
			expected:          5,
		},
		{
			name:              "both, below min floors to zero",
			nodeCount:         10,
			percentageOfNodes: 20,
			minServers:        ptr.To[int32](3),
			maxServers:        ptr.To[int32](10),
			expected:          0,
		},
		{
			name:              "both, above max caps",
			nodeCount:         100,
			percentageOfNodes: 50,
			minServers:        ptr.To[int32](3),
			maxServers:        ptr.To[int32](10),
			expected:          10,
		},
		{
			name:              "min equals max, at value",
			nodeCount:         10,
			percentageOfNodes: 50,
			minServers:        ptr.To[int32](5),
			maxServers:        ptr.To[int32](5),
			expected:          5,
		},
		{
			name:              "min equals max, below floors to zero",
			nodeCount:         10,
			percentageOfNodes: 40,
			minServers:        ptr.To[int32](5),
			maxServers:        ptr.To[int32](5),
			expected:          0,
		},
		// Overflow and bounds safety
		{
			name:              "node count at MaxInt32 with 100%",
			nodeCount:         math.MaxInt32,
			percentageOfNodes: 100,
			expected:          math.MaxInt32,
		},
		{
			name:              "node count at MaxInt32 with 50%",
			nodeCount:         math.MaxInt32,
			percentageOfNodes: 50,
			expected:          int32(int64(math.MaxInt32) * 50 / 100),
		},
		{
			name:              "node count at MaxInt32 with 1%",
			nodeCount:         math.MaxInt32,
			percentageOfNodes: 1,
			expected:          int32(int64(math.MaxInt32) / 100),
		},
		{
			name:              "node count exceeds MaxInt32 returns error",
			nodeCount:         math.MaxInt32 + 1,
			percentageOfNodes: 50,
			expectErr:         true,
		},
		{
			name:              "very large node count returns error",
			nodeCount:         math.MaxInt64 / 2,
			percentageOfNodes: 50,
			expectErr:         true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := CalculateDesiredReplicas(tc.nodeCount, tc.percentageOfNodes, tc.minServers, tc.maxServers)
			if tc.expectErr {
				if err == nil {
					t.Errorf("CalculateDesiredReplicas(%d, %d, %v, %v) expected error, got %d",
						tc.nodeCount, tc.percentageOfNodes, tc.minServers, tc.maxServers, got)
				}
				return
			}
			if err != nil {
				t.Fatalf("CalculateDesiredReplicas(%d, %d, %v, %v) unexpected error: %v",
					tc.nodeCount, tc.percentageOfNodes, tc.minServers, tc.maxServers, err)
			}
			if got != tc.expected {
				t.Errorf("CalculateDesiredReplicas(%d, %d, %v, %v) = %d, want %d",
					tc.nodeCount, tc.percentageOfNodes, tc.minServers, tc.maxServers, got, tc.expected)
			}
		})
	}
}
