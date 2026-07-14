// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

// Package git provides utilities for interacting with git repositories.
package git

import (
	"context"
	"os"
	"os/exec"
	"strings"

	"github.com/Azure/dacs/test/pkg/logger"
)

// log is the default logger for the git package.
var log = logger.New()

// GetCommit gets the current git commit hash.
// Prefers GIT_COMMIT env var (set by pipeline) over git command.
func GetCommit(ctx context.Context) string {
	if commit := os.Getenv("GIT_COMMIT"); commit != "" {
		return commit
	}
	cmd := exec.CommandContext(ctx, "git", "rev-parse", "HEAD")
	output, err := cmd.Output()
	if err != nil {
		log.Error(err, "Failed to get git commit")
		return "<unknown>"
	}
	return strings.TrimSpace(string(output))
}

// GetBranch gets the current git branch name.
// Prefers GIT_BRANCH env var (set by pipeline) over git command (detached HEAD in CI).
func GetBranch(ctx context.Context) string {
	if branch := os.Getenv("GIT_BRANCH"); branch != "" {
		return branch
	}
	cmd := exec.CommandContext(ctx, "git", "rev-parse", "--abbrev-ref", "HEAD")
	output, err := cmd.Output()
	if err != nil {
		log.Error(err, "Failed to get git branch")
		return "<unknown>"
	}
	return strings.TrimSpace(string(output))
}
