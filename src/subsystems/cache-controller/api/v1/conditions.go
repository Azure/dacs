/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package v1

// Condition type constants for Cache status conditions.
//
// All conditions use positive polarity (True = good/done). This means the
// "normal" healthy state for every condition is Status=True, and Status=False
// signals a problem. Positive polarity is the recommended convention for
// Kubernetes conditions because it integrates with kstatus reconciliation
// and tooling such as `kubectl wait --for=condition=Ready`.
//
// Reference: https://github.com/kubernetes-sigs/cli-utils/blob/master/pkg/kstatus/README.md
const (
	// ReadyCondition is the aggregate condition. True when all
	// sub-conditions are True; False otherwise.
	ReadyCondition = "Ready"

	// ReplicasReadyCondition indicates whether the desired number of
	// StatefulSet replicas are running and ready.
	ReplicasReadyCondition = "ReplicasReady"

	// PodsScheduledCondition indicates whether all cache server pods
	// have been successfully scheduled. False when pods are stuck or
	// scheduling errors occurred.
	PodsScheduledCondition = "PodsScheduled"
)

// Condition reason constants. Exported so tests and external consumers can
// match against well-known reason strings rather than brittle literals.
const (
	// Ready condition reasons.
	ReasonAllConditionsMet = "AllConditionsMet"

	// ReplicasReady condition reasons.
	ReasonAllReplicasReady     = "AllReplicasReady"
	ReasonSomeReplicasNotReady = "SomeReplicasNotReady"
	ReasonNoReplicasReady      = "NoReplicasReady"
	ReasonDesiredCountUnknown  = "DesiredCountUnknown"
	ReasonStatefulSetNotFound  = "StatefulSetNotFound"
	ReasonStatefulSetGetError  = "StatefulSetGetError"

	// PodsScheduled condition reasons.
	ReasonAllPodsScheduled           = "AllPodsScheduled"
	ReasonPodsStuck                  = "PodsStuck"
	ReasonPodsWaitingForNodeRecovery = "PodsWaitingForNodeRecovery"
	ReasonSchedulingError            = "SchedulingError"

	// Shared reasons used across multiple condition types.
	ReasonNotEvaluated  = "NotEvaluated"
	ReasonNodeListError = "NodeListError"
	ReasonPodListError  = "PodListError"
)
