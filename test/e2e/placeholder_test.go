package e2e

import (
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
)

var _ = Describe("DACS Cache Server", Label("e2e"), Ordered, func() {
	// Set default timeouts for all Eventually/Consistently calls in e2e tests
	SetDefaultEventuallyTimeout(2 * time.Minute)
	SetDefaultEventuallyPollingInterval(time.Second)
	EnforceDefaultTimeoutsWhenUsingContexts()

	It("should pass", func() {
		Expect(true).To(BeTrue())
	})
})
