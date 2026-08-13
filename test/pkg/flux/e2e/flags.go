/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# E2e test specific flags
# --------------------------------------------------------------------------------------------------------------------
*/

package e2e

import (
	"flag"

	"github.com/Azure/dacs/test/pkg/flux"
)

type Options struct {
	// Common flags (includes StorageAccount, ClientImage, WorkloadIdentity)
	*flux.Options
}

// AddFlags adds e2e test specific flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	var commonOpts flux.Options
	if err := flux.AddFlags(fs, &commonOpts); err != nil {
		return err
	}
	opts.Options = &commonOpts

	return nil
}
