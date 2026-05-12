/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

// Event reasons recorded on Cache resources by the controller.
const (
	// EventReasonScaling is recorded when the controller scales the
	// cacheserver StatefulSet up or down.
	EventReasonScaling = "Scaling"

	// EventReasonRecreated is recorded when the controller recreates the
	// cacheserver StatefulSet (for example after detecting drift in immutable
	// fields).
	EventReasonRecreated = "RecreatedStatefulSet"
)

// Event actions recorded on Cache resources by the controller.
const (
	// EventActionScale corresponds to scaling the cacheserver StatefulSet.
	EventActionScale = "ScaleReplicas"

	// EventActionRecreate corresponds to recreating the cacheserver
	// StatefulSet.
	EventActionRecreate = "RecreateStatefulSet"
)
