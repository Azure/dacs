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
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/util/validation"
	"k8s.io/apimachinery/pkg/util/validation/field"
)

// ValidateTolerations applies Kubernetes toleration semantics at admission and
// reconciliation, including when the admission webhook is disabled.
func ValidateTolerations(tolerations []corev1.Toleration, fldPath *field.Path) field.ErrorList {
	var allErrs field.ErrorList
	for i, toleration := range tolerations {
		path := fldPath.Index(i)
		if toleration.Key != "" {
			for _, msg := range validation.IsQualifiedName(toleration.Key) {
				allErrs = append(allErrs, field.Invalid(path.Child("key"), toleration.Key, msg))
			}
		} else if toleration.Operator != corev1.TolerationOpExists {
			allErrs = append(allErrs, field.Invalid(path.Child("operator"), toleration.Operator, "must be Exists when key is empty"))
		}
		switch toleration.Operator {
		case "", corev1.TolerationOpEqual:
			for _, msg := range validation.IsValidLabelValue(toleration.Value) {
				allErrs = append(allErrs, field.Invalid(path.Child("value"), toleration.Value, msg))
			}
		case corev1.TolerationOpExists:
			if toleration.Value != "" {
				allErrs = append(allErrs, field.Invalid(path.Child("value"), toleration.Value, "must be empty when operator is Exists"))
			}
		default:
			allErrs = append(allErrs, field.NotSupported(path.Child("operator"), toleration.Operator, []string{"Equal", "Exists"}))
		}
		switch toleration.Effect {
		case "", corev1.TaintEffectNoSchedule, corev1.TaintEffectPreferNoSchedule, corev1.TaintEffectNoExecute:
		default:
			allErrs = append(allErrs, field.NotSupported(path.Child("effect"), toleration.Effect, []string{"NoSchedule", "PreferNoSchedule", "NoExecute"}))
		}
		if toleration.TolerationSeconds != nil && toleration.Effect != corev1.TaintEffectNoExecute {
			allErrs = append(allErrs, field.Invalid(path.Child("tolerationSeconds"), *toleration.TolerationSeconds, "may only be set when effect is NoExecute"))
		}
	}
	return allErrs
}
