package logger

import (
	"github.com/go-logr/logr"
	. "github.com/onsi/ginkgo/v2"
	"k8s.io/klog/v2/textlogger"
)

// New returns a logr.Logger that writes to GinkgoWriter with klog-style formatting.
// Output format: I0107 20:30:45.123456 message key=value
func New() logr.Logger {
	config := textlogger.NewConfig(textlogger.Output(GinkgoWriter))
	return textlogger.NewLogger(config)
}
