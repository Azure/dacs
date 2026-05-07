/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package main

import (
	"testing"

	"go.uber.org/zap/zapcore"
	"gotest.tools/v3/assert"
)

func TestGetZapLogLevelInfo(t *testing.T) {
	level := getZapLogLevel("info")
	assert.Equal(t, zapcore.InfoLevel, level)
}

func TestGetZapLogLevelDebug(t *testing.T) {
	level := getZapLogLevel("debug")
	assert.Equal(t, zapcore.DebugLevel, level)
}

func TestGetZapLogLevelError(t *testing.T) {
	level := getZapLogLevel("error")
	assert.Equal(t, zapcore.ErrorLevel, level)
}

func TestGetZapLogLevelWarn(t *testing.T) {
	level := getZapLogLevel("warn")
	assert.Equal(t, zapcore.WarnLevel, level)
}

func TestGetZapLogLevelInvalid(t *testing.T) {
	// Invalid log level should default to info
	level := getZapLogLevel("invalid-level")
	assert.Equal(t, zapcore.InfoLevel, level)
}

func TestGetZapLogLevelEmpty(t *testing.T) {
	// Empty log level should default to info
	level := getZapLogLevel("")
	assert.Equal(t, zapcore.InfoLevel, level)
}
