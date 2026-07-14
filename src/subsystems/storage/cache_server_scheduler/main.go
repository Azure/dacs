// Copyright (c) Microsoft Corporation.
// Licensed under the Apache License, Version 2.0.

package main

import (
	"io"
	"os"
	"time"

	log "github.com/sirupsen/logrus"
	"github.com/sirupsen/logrus/hooks/writer"
)

func initLogger(logLevel string) {
	if level, err := log.ParseLevel(logLevel); err != nil {
		log.Fatalf("Invalid log level: %s", err.Error())
	} else {
		log.SetLevel(level)
	}
	// From https://github.com/sirupsen/logrus/tree/master/hooks/writer#usage
	// Send all logs to nowhere by default
	log.SetOutput(io.Discard)
	log.SetFormatter(&log.TextFormatter{
		TimestampFormat: "2006-01-02 15:04:05",
		FullTimestamp:   true,
	})

	// Send logs with level higher than warning to stderr
	log.AddHook(&writer.Hook{
		Writer: os.Stderr,
		LogLevels: []log.Level{
			log.PanicLevel,
			log.FatalLevel,
			log.ErrorLevel,
		},
	})
	// Send info and debug logs to stdout
	log.AddHook(&writer.Hook{
		Writer: os.Stdout,
		LogLevels: []log.Level{
			log.WarnLevel,
			log.InfoLevel,
			log.DebugLevel,
		},
	})
}

func main() {
	initLogger(GetConfig().LogLevel)
	log.Infof("Config: %v", GetConfig())

	schedulerSleepInterval := GetConfig().SchedulerPollInterval
	log.Infof("Starting CacheServer Scheduler loop with sleep interval: %s", schedulerSleepInterval)
	for {
		nodes, err := GetNodesFromK8s()
		if err != nil {
			log.Fatalf("Error getting nodes from k8s: %s", err.Error())
		}

		pods, err := GetPodsFromK8s()
		if err != nil {
			log.Fatalf("Error getting pods from k8s: %s", err.Error())
		}

		if GetConfig().EnableSchedulerCoordination {
			InitSchedulerCoordinator()
		}

		schedulerInstructions := SchedulerPeriodicStep(pods, nodes, GetConfig().EnableSchedulerCoordination, GetConfig().CacheserverNamespace)
		exectionErrors := ExecuteSchedulerInstructionsOnK8s(schedulerInstructions)
		if len(exectionErrors) > 0 {
			log.Errorf("Errors while executing schedule instructions: %v, errorCount: %d", exectionErrors, len(exectionErrors))
		}
		log.Debugf("Sleeping for %s", schedulerSleepInterval)
		time.Sleep(schedulerSleepInterval)
	}
}
