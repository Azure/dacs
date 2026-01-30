/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# E2e test specific flags
# --------------------------------------------------------------------------------------------------------------------
*/

package e2e

import (
	"flag"

	"github.com/Azure/dacs/test/pkg/flux"
)

type Options struct {
	// Common report/output flags
	*flux.Options
}

// AddFlags adds performance test specific flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	var commonOpts flux.Options
	if err := flux.AddFlags(fs, &commonOpts); err != nil {
		return err
	}
	opts.Options = &commonOpts
	return nil
}
