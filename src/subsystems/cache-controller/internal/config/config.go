/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package config

import (
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"

	log "github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	corev1 "k8s.io/api/core/v1"
)

const (
	ConfEnvPrefix         = "CACHE_CONTROLLER"
	DefaultConfigFilePath = "./cache-controller-config.yaml"
)

type Config struct {
	LogLevel                  string            `yaml:"logLevel" mapstructure:"logLevel"`
	CacheServerImage          string            `yaml:"cacheServerImage" mapstructure:"cacheServerImage"`
	ImagePullPolicy           corev1.PullPolicy `yaml:"imagePullPolicy" mapstructure:"imagePullPolicy"`
	PodStuckThresholdDuration time.Duration     `yaml:"podStuckThresholdDuration" mapstructure:"podStuckThresholdDuration"`
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
		var configFilePath string
		// Explicit override via env var
		if p := os.Getenv(ConfEnvPrefix + "_CONFIG_PATH"); p != "" {
			configFilePath = p
		} else {
			// Prefer next to executable if present
			if exePath, err := os.Executable(); err == nil && exePath != "" {
				exeDir := filepath.Dir(exePath)
				candidate := path.Join(exeDir, DefaultConfigFilePath)
				if _, statErr := os.Stat(candidate); statErr == nil {
					configFilePath = candidate
				}
			}
			// Fallback to current working directory if present
			if configFilePath == "" {
				if _, statErr := os.Stat(DefaultConfigFilePath); statErr == nil {
					configFilePath = DefaultConfigFilePath
				}
			}
		}
		if configFilePath == "" {
			log.Infof("InitConfig not called; no config file found, using env/defaults")
		} else {
			log.Infof("InitConfig is not called, will load it from path: %s", configFilePath)
		}
		InitConfig(configFilePath)
	}
	return configInstance
}

func loadConfigInternal(cfgPath string) (*Config, error) {
	var (
		err    error
		config Config
	)

	// Env vars always available
	viper.SetEnvPrefix(ConfEnvPrefix)
	viper.AutomaticEnv()
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))

	// If a config file path is provided, try to read it; tolerate not-found
	if strings.TrimSpace(cfgPath) != "" {
		viper.SetConfigFile(cfgPath)
		if err = viper.ReadInConfig(); err != nil {
			var nf viper.ConfigFileNotFoundError
			if !errors.As(err, &nf) {
				return nil, fmt.Errorf("reading config file: %w", err)
			}
			// Not found: continue with env/defaults
		}
	}

	if err = viper.Unmarshal(&config); err != nil {
		return nil, fmt.Errorf("decode config struct: %w", err)
	}

	if err = config.validate(); err != nil {
		return nil, fmt.Errorf("config validation failed: %w", err)
	}

	return &config, nil
}

// validate checks that all mandatory configuration fields are set
func (c *Config) validate() error {
	var missingFields []string

	if c.CacheServerImage == "" {
		missingFields = append(missingFields, "cacheServerImage")
	}

	if len(missingFields) > 0 {
		return fmt.Errorf("missing required configuration fields: %s", strings.Join(missingFields, ", "))
	}

	return nil
}
