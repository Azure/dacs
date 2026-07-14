/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
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
