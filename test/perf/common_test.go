package perf

import "github.com/Azure/dacs/test/pkg/common"

// getClientImage returns the dacs client image from command-line flag, or falls back to default.
func getClientImage() string {
	if opts.ClientImage != "" {
		return opts.ClientImage
	}
	return common.GetDefaultImage()
}
