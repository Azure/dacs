/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Common flags for all test suites
# --------------------------------------------------------------------------------------------------------------------
*/

package common

import "flag"

// Options holds common flags for flux-based tests
type Options struct {
	// Common report/output flags
	// JunitReport is the report file generated for consumption by Junit.
	JunitReport string
	// SupportBundleDir is the directory where support bundles are written.
	SupportBundleDir string
	// SummaryReport is the path to write the summary report
	SummaryReport string
}

// AddFlags adds common flux test flags to the provided FlagSet
func AddFlags(fs *flag.FlagSet, opts *Options) error {
	fs.StringVar(&opts.JunitReport, "junit-report", "", "The report file generated for consumption by Junit.")
	fs.StringVar(&opts.SupportBundleDir, "support-bundle-dir", "", "The directory where support bundles are written.")
	fs.StringVar(&opts.SummaryReport, "summary-report", "", "The path to write the summary report")

	return nil
}
