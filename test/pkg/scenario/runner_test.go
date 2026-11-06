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

package scenario

import (
	"fmt"
	"os"
	"testing"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
)

func TestScenarioRunner(t *testing.T) {
	RegisterFailHandler(Fail)
	RunSpecs(t, "Scenario Runner")
}

var _ = Describe("Model loading job requirements", func() {
	BeforeEach(func() {
		GinkgoT().Chdir("../../..")
	})

	DescribeTable("uses checkout requirements only for RunAI",
		func(ctx SpecContext, method AccessMethod) {
			requirements, err := os.ReadFile("Docker/requirements/runai-model-streamer.txt")
			Expect(err).NotTo(HaveOccurred(), "reading checkout RunAI requirements")
			runner := ModelLoadingRunner{
				Config: ModelLoadingConfig{Namespace: "test", TestImage: "cache-test-client:older"},
				Scale:  Scale7B,
				Method: method,
				Cache:  NoCache,
			}
			job, err := runner.buildJob(ctx, "model-loading")
			Expect(err).NotTo(HaveOccurred(), "building %s job", method)
			script := job.Spec.Template.Spec.Containers[0].Args[0]
			injected := fmt.Sprintf("cat > /tmp/runai-model-streamer-requirements.txt << 'RUNAI_REQUIREMENTS_EOF'\n%s\nRUNAI_REQUIREMENTS_EOF", requirements)
			if method == RunAIStreamer {
				Expect(script).To(ContainSubstring(injected), "injecting the full checkout lock for %s", method)
				Expect(script).To(ContainSubstring("python3 -m pip install --no-cache-dir --require-hashes \\\n    -r /tmp/runai-model-streamer-requirements.txt"), "installing the injected lock with hash verification")
			} else {
				Expect(script).NotTo(ContainSubstring("runai-model-streamer-requirements.txt"), "leaving %s dependency setup unchanged", method)
			}
			Expect(script).NotTo(ContainSubstring("/app/runai-model-streamer-requirements.txt"), "not using the image's potentially stale requirements")
		},
		Entry("RunAI with an older client image", RunAIStreamer),
		Entry("blobfile with an older client image", Blobfile),
	)

	DescribeTable("requires the checkout lock only for RunAI",
		func(ctx SpecContext, method AccessMethod) {
			GinkgoT().Chdir(GinkgoT().TempDir())
			runner := ModelLoadingRunner{Method: method}
			_, err := runner.buildJob(ctx, "model-loading")
			if method == RunAIStreamer {
				Expect(err).To(MatchError(os.ErrNotExist), "failing explicitly when the checkout lock is unavailable")
			} else {
				Expect(err).NotTo(HaveOccurred(), "building %s without a RunAI lock", method)
			}
		},
		Entry("RunAI fails without its lock", RunAIStreamer),
		Entry("blobfile does not need a RunAI lock", Blobfile),
	)
})
