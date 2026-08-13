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

package main

import (
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"

	log "github.com/sirupsen/logrus"
	"github.com/spf13/viper"
)

const (
	ConfEnvPrefix         = "CACHESERVER_SCHEDULER"
	DefaultConfigFilePath = "./cacheserver-scheduler-config.yaml"
)

type Config struct {
	LogLevel                    string        `yaml:"logLevel" mapstructure:"logLevel"`
	SchedulerPollInterval       time.Duration `yaml:"schedulerPollInterval" mapstructure:"schedulerPollInterval"`
	ProcessingUnitLabelKey      string        `yaml:"processingUnitLabelKey" mapstructure:"processingUnitLabelKey"`
	CacheserverNamespace        string        `yaml:"cacheserverNamespace" mapstructure:"cacheserverNamespace"`
	EnableSchedulerCoordination bool          `yaml:"enableSchedulerCoordination" mapstructure:"enableSchedulerCoordination"`
	IsHostPathMountEnabled      bool          `yaml:"isHostPathMountEnabled" mapstructure:"isHostPathMountEnabled"`
}

var (
	once           sync.Once
	configInstance *Config
)

// Initialize the configuration singleton by loading the configuration from the specified file path and overriding the values with any environment variables.
// This function is called only once, and any subsequent calls to InitConfig will have no effect
func InitConfig(filePath string) {
	once.Do(func() {
		config, err := loadConfigInternal(filePath)
		if err != nil {
			log.Fatalf("Failed to load config from %s: %v", filePath, err)
		}
		configInstance = config
	})
}

// Retrieve the current configuration instance.
// If the configuration has not been initialized yet, the function will call InitConfig with the default file path and return the resulting configuration
func GetConfig() *Config {
	if configInstance == nil {
		pwd := filepath.Dir(os.Args[0])
		configFilePath := path.Join(pwd, DefaultConfigFilePath)
		log.Infof("InitConfig is not called, will load it from default path: %s", configFilePath)
		InitConfig(configFilePath)
	}
	return configInstance
}

func loadConfigInternal(cfgPath string) (*Config, error) {
	var (
		err    error
		config Config
	)

	cfgName := path.Base(cfgPath)
	viper.SetConfigName(strings.TrimSuffix(cfgName, filepath.Ext(cfgName)))
	viper.AddConfigPath(path.Dir(cfgPath))
	viper.SetEnvPrefix(ConfEnvPrefix)
	viper.AutomaticEnv()
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))

	if err = viper.ReadInConfig(); err != nil {
		return nil, fmt.Errorf("Reading config file error %v", err)
	}

	if err = viper.Unmarshal(&config); err != nil {
		return nil, fmt.Errorf("Decode config struct error %v", err)
	}

	return &config, nil
}
