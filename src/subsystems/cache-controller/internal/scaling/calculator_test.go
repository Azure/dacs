/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package scaling

import (
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
			name:              "zero nodes returns 1",
			nodeCount:         0,
			percentageOfNodes: 50,
			expected:          1,
		},
		{
			name:              "rounds up: 50% of 3 nodes = ceil(1.5) = 2",
			nodeCount:         3,
			percentageOfNodes: 50,
			expected:          2,
		},
		{
			name:              "100% of 5 nodes",
			nodeCount:         5,
			percentageOfNodes: 100,
			expected:          5,
		},
		{
			name:              "10% of 1 node rounds up to 1",
			nodeCount:         1,
			percentageOfNodes: 10,
			expected:          1,
		},
		{
			name:              "25% of 7 nodes = ceil(1.75) = 2",
			nodeCount:         7,
			percentageOfNodes: 25,
			expected:          2,
		},
		// Error cases
		{
			name:              "negative node count returns error",
			nodeCount:         -5,
			percentageOfNodes: 50,
			expectErr:         true,
		},
		{
			name:              "zero percentage returns error",
			nodeCount:         10,
			percentageOfNodes: 0,
			expectErr:         true,
		},
		{
			name:              "percentage over 100 returns error",
			nodeCount:         10,
			percentageOfNodes: 150,
			expectErr:         true,
		},
		{
			name:              "max < min returns error",
			nodeCount:         10,
			percentageOfNodes: 50,
			minServers:        ptr.To(int32(7)),
			maxServers:        ptr.To(int32(3)),
			expectErr:         true,
		},
		// Min/max clamping tests
		{
			name:              "min clamping: 10% of 2 nodes = 1, min=3 => 3",
			nodeCount:         2,
			percentageOfNodes: 10,
			minServers:        ptr.To(int32(3)),
			expected:          3,
		},
		{
			name:              "max clamping: 100% of 10 nodes = 10, max=5 => 5",
			nodeCount:         10,
			percentageOfNodes: 100,
			maxServers:        ptr.To(int32(5)),
			expected:          5,
		},
		{
			name:              "min and max: 50% of 20 nodes = 10, min=2 max=8 => 8",
			nodeCount:         20,
			percentageOfNodes: 50,
			minServers:        ptr.To(int32(2)),
			maxServers:        ptr.To(int32(8)),
			expected:          8,
		},
		{
			name:              "min and max: 50% of 2 nodes = 1, min=3 max=8 => 3",
			nodeCount:         2,
			percentageOfNodes: 50,
			minServers:        ptr.To(int32(3)),
			maxServers:        ptr.To(int32(8)),
			expected:          3,
		},
		{
			name:              "min and max: value within range stays unchanged",
			nodeCount:         10,
			percentageOfNodes: 50,
			minServers:        ptr.To(int32(2)),
			maxServers:        ptr.To(int32(8)),
			expected:          5,
		},
		{
			name:              "zero nodes with min returns min",
			nodeCount:         0,
			percentageOfNodes: 50,
			minServers:        ptr.To(int32(4)),
			expected:          4,
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
			name:              "33% of 10 nodes = ceil(3.3) = 4",
			nodeCount:         10,
			percentageOfNodes: 33,
			expected:          4,
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
