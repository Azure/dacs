/*
Copyright (c) Microsoft Corporation. All rights reserved.
*/

package main

import (
	"crypto/tls"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"

	// Import all Kubernetes client auth plugins (e.g. Azure, GCP, OIDC, etc.)
	// to ensure that exec-entrypoint and run can make use of them.
	_ "k8s.io/client-go/plugin/pkg/client/auth"

	"github.com/open-policy-agent/cert-controller/pkg/rotator"
	log "github.com/sirupsen/logrus"
	"github.com/sirupsen/logrus/hooks/writer"
	"go.uber.org/zap/zapcore"
	appsv1 "k8s.io/api/apps/v1"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/runtime"
	"k8s.io/apimachinery/pkg/types"
	utilruntime "k8s.io/apimachinery/pkg/util/runtime"
	clientgoscheme "k8s.io/client-go/kubernetes/scheme"
	ctrl "sigs.k8s.io/controller-runtime"
	"sigs.k8s.io/controller-runtime/pkg/cache"
	"sigs.k8s.io/controller-runtime/pkg/client"
	"sigs.k8s.io/controller-runtime/pkg/healthz"
	"sigs.k8s.io/controller-runtime/pkg/log/zap"
	"sigs.k8s.io/controller-runtime/pkg/metrics/filters"
	metricsserver "sigs.k8s.io/controller-runtime/pkg/metrics/server"
	"sigs.k8s.io/controller-runtime/pkg/webhook"

	storagev1 "github.com/Azure/dacs/cache-controller/api/v1"
	"github.com/Azure/dacs/cache-controller/internal/config"
	"github.com/Azure/dacs/cache-controller/internal/controller"
	"github.com/Azure/dacs/cache-controller/internal/management"
	"github.com/Azure/dacs/cache-controller/internal/scaling"
	"github.com/Azure/dacs/cache-controller/internal/scheduling"
	// +kubebuilder:scaffold:imports
)

var (
	scheme   = runtime.NewScheme()
	setupLog = ctrl.Log.WithName("setup")
)

func init() {
	utilruntime.Must(clientgoscheme.AddToScheme(scheme))

	utilruntime.Must(storagev1.AddToScheme(scheme))
	// +kubebuilder:scaffold:scheme
}

func getZapLogLevel(logLevel string) zapcore.Level {
	zapLevel, err := zapcore.ParseLevel(logLevel)
	if err != nil {
		setupLog.Error(err, "could not parse zap log level, defaulting to info", "logLevel", logLevel)
		return zapcore.InfoLevel
	}
	return zapLevel
}

func initLogger(logLevel string) {
	// We use 2 logging libraries:
	// 1. logrus for general logging.
	// 2. controller-runtime's zap logger for k8s controller logs.
	// Here we initialize both.

	// Initialize logrus
	if level, err := log.ParseLevel(logLevel); err != nil {
		log.Fatalf("Invalid log level: %s", err.Error())
	} else {
		log.SetLevel(level)
	}

	// Setup logrus hooks to separate stdout and stderr
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

// nolint:gocyclo
func main() {
	var metricsAddr string
	var metricsCertPath, metricsCertName, metricsCertKey string
	var webhookCertPath, webhookCertName, webhookCertKey string
	var enableLeaderElection bool
	var probeAddr string
	var secureMetrics bool
	var enableHTTP2 bool
	var enableWebhooks bool
	var webhookPort int
	var namespace string
	var webhookServiceName string
	var certSecretName string
	var mutatingWebhookConfigName string
	var validatingWebhookConfigName string
	var tlsOpts []func(*tls.Config)
	flag.StringVar(&metricsAddr, "metrics-bind-address", "0", "The address the metrics endpoint binds to. "+
		"Use :8443 for HTTPS or :8080 for HTTP, or leave as 0 to disable the metrics service.")
	flag.StringVar(&probeAddr, "health-probe-bind-address", ":8081", "The address the probe endpoint binds to.")
	flag.BoolVar(&enableLeaderElection, "leader-elect", false,
		"Enable leader election for controller manager. "+
			"Enabling this will ensure there is only one active controller manager.")
	flag.BoolVar(&secureMetrics, "metrics-secure", true,
		"If set, the metrics endpoint is served securely via HTTPS. Use --metrics-secure=false to use HTTP instead.")
	flag.StringVar(&webhookCertPath, "webhook-cert-path", "/tmp/k8s-webhook-server/serving-certs",
		"The directory that contains the webhook certificate.")
	flag.StringVar(&webhookCertName, "webhook-cert-name", "tls.crt", "The name of the webhook certificate file.")
	flag.StringVar(&webhookCertKey, "webhook-cert-key", "tls.key", "The name of the webhook key file.")
	flag.StringVar(&metricsCertPath, "metrics-cert-path", "",
		"The directory that contains the metrics server certificate.")
	flag.StringVar(&metricsCertName, "metrics-cert-name", "tls.crt", "The name of the metrics server certificate file.")
	flag.StringVar(&metricsCertKey, "metrics-cert-key", "tls.key", "The name of the metrics server key file.")
	flag.BoolVar(&enableHTTP2, "enable-http2", false,
		"If set, HTTP/2 will be enabled for the metrics and webhook servers")
	flag.BoolVar(&enableWebhooks, "enable-webhooks", false, "Enable admission webhooks for defaulting and validation.")
	flag.IntVar(&webhookPort, "webhook-port", 9443, "The port the webhook server listens on.")
	flag.StringVar(&namespace, "namespace", "cache-controller-system",
		"The namespace where the controller is deployed.")
	flag.StringVar(&webhookServiceName, "webhook-service-name",
		"cache-controller-webhook-service", "The name of the webhook service.")
	flag.StringVar(&certSecretName, "cert-secret-name", "webhook-server-cert",
		"The name of the secret containing the webhook certificates.")
	flag.StringVar(&mutatingWebhookConfigName, "mutating-webhook-config-name",
		"cache-controller-mutating-webhook-configuration",
		"The name of the MutatingWebhookConfiguration.")
	flag.StringVar(&validatingWebhookConfigName, "validating-webhook-config-name",
		"cache-controller-validating-webhook-configuration",
		"The name of the ValidatingWebhookConfiguration.")

	internalConfig := config.GetConfig()
	setupLog.Info("Configuration loaded", "fullConfig", internalConfig)
	initLogger(internalConfig.LogLevel)

	opts := zap.Options{
		Development: true,
		Level:       getZapLogLevel(internalConfig.LogLevel),
	}
	opts.BindFlags(flag.CommandLine)
	flag.Parse()
	ctrl.SetLogger(zap.New(zap.UseFlagOptions(&opts)))

	// if the enable-http2 flag is false (the default), http/2 should be disabled
	// due to its vulnerabilities. More specifically, disabling http/2 will
	// prevent from being vulnerable to the HTTP/2 Stream Cancellation and
	// Rapid Reset CVEs. For more information see:
	// - https://github.com/advisories/GHSA-qppj-fm5r-hxr3
	// - https://github.com/advisories/GHSA-4374-p667-p6c8
	disableHTTP2 := func(c *tls.Config) {
		setupLog.Info("disabling http/2")
		c.NextProtos = []string{"http/1.1"}
	}

	if !enableHTTP2 {
		tlsOpts = append(tlsOpts, disableHTTP2)
	}

	// Initial webhook TLS options
	webhookTLSOpts := tlsOpts
	webhookServerOptions := webhook.Options{
		TLSOpts: webhookTLSOpts,
		Port:    webhookPort,
	}

	if enableWebhooks && len(webhookCertPath) > 0 {
		setupLog.Info("Initializing webhook certificate watcher using provided certificates",
			"webhook-cert-path", webhookCertPath, "webhook-cert-name", webhookCertName, "webhook-cert-key", webhookCertKey)

		webhookServerOptions.CertDir = webhookCertPath
		webhookServerOptions.CertName = webhookCertName
		webhookServerOptions.KeyName = webhookCertKey
	}

	webhookServer := webhook.NewServer(webhookServerOptions)

	// Metrics endpoint is enabled in 'config/default/kustomization.yaml'. The Metrics options configure the server.
	// More info:
	// - https://pkg.go.dev/sigs.k8s.io/controller-runtime@v0.22.4/pkg/metrics/server
	// - https://book.kubebuilder.io/reference/metrics.html
	metricsServerOptions := metricsserver.Options{
		BindAddress:   metricsAddr,
		SecureServing: secureMetrics,
		TLSOpts:       tlsOpts,
	}

	if secureMetrics {
		// FilterProvider is used to protect the metrics endpoint with authn/authz.
		// These configurations ensure that only authorized users and service accounts
		// can access the metrics endpoint. The RBAC are configured in 'config/rbac/kustomization.yaml'. More info:
		// https://pkg.go.dev/sigs.k8s.io/controller-runtime@v0.22.4/pkg/metrics/filters#WithAuthenticationAndAuthorization
		metricsServerOptions.FilterProvider = filters.WithAuthenticationAndAuthorization
	}

	// If the certificate is not specified, controller-runtime will automatically
	// generate self-signed certificates for the metrics server. While convenient for development and testing,
	// this setup is not recommended for production.
	//
	// TODO(user): If you enable certManager, uncomment the following lines:
	// - [METRICS-WITH-CERTS] at config/default/kustomization.yaml to generate and use certificates
	// managed by cert-manager for the metrics server.
	// - [PROMETHEUS-WITH-CERTS] at config/prometheus/kustomization.yaml for TLS certification.
	if len(metricsCertPath) > 0 {
		setupLog.Info("Initializing metrics certificate watcher using provided certificates",
			"metrics-cert-path", metricsCertPath, "metrics-cert-name", metricsCertName, "metrics-cert-key", metricsCertKey)

		metricsServerOptions.CertDir = metricsCertPath
		metricsServerOptions.CertName = metricsCertName
		metricsServerOptions.KeyName = metricsCertKey
	}

	mgr, err := ctrl.NewManager(ctrl.GetConfigOrDie(), ctrl.Options{
		Scheme:                 scheme,
		Metrics:                metricsServerOptions,
		WebhookServer:          webhookServer,
		HealthProbeBindAddress: probeAddr,
		LeaderElection:         enableLeaderElection,
		LeaderElectionID:       "e7aa1f26.azure.com",
		Cache: cache.Options{
			ByObject: map[client.Object]cache.ByObject{
				&corev1.Pod{}: {
					Label: controller.CacheServerSelector,
				},
				&appsv1.StatefulSet{}: {
					Label: controller.CacheServerSelector,
				},
				&corev1.Service{}: {
					Label: controller.CacheServerSelector,
				},
				&corev1.ConfigMap{}: {
					Label: controller.CacheServerSelector,
				},
			},
		},
		// LeaderElectionReleaseOnCancel defines if the leader should step down voluntarily
		// when the Manager ends. This requires the binary to immediately end when the
		// Manager is stopped, otherwise, this setting is unsafe. Setting this significantly
		// speeds up voluntary leader transitions as the new leader don't have to wait
		// LeaseDuration time first.
		//
		// In the default scaffold provided, the program ends immediately after
		// the manager stops, so would be fine to enable this option. However,
		// if you are doing or is intended to do any operation such as perform cleanups
		// after the manager stops then its usage might be unsafe.
		// LeaderElectionReleaseOnCancel: true,
	})
	if err != nil {
		setupLog.Error(err, "unable to start manager")
		os.Exit(1)
	}

	// Setup certificate rotation for webhooks using cert-controller
	// This automatically generates and rotates certificates for webhook server
	certSetupFinished := make(chan struct{})
	if enableWebhooks {
		setupLog.Info("Setting up certificate rotation for webhooks")

		webhooks := []rotator.WebhookInfo{
			{
				Name: mutatingWebhookConfigName,
				Type: rotator.Mutating,
			},
			{
				Name: validatingWebhookConfigName,
				Type: rotator.Validating,
			},
		}

		if err := rotator.AddRotator(mgr, &rotator.CertRotator{
			SecretKey: types.NamespacedName{
				Namespace: namespace,
				Name:      certSecretName,
			},
			CertDir:        webhookCertPath,
			CAName:         "cache-controller-ca",
			CAOrganization: "Cache Controller",
			DNSName:        fmt.Sprintf("%s.%s.svc", webhookServiceName, namespace),
			ExtraDNSNames: []string{
				fmt.Sprintf("%s.%s.svc.cluster.local", webhookServiceName, namespace),
				fmt.Sprintf("%s.%s", webhookServiceName, namespace),
			},
			Webhooks:             webhooks,
			IsReady:              certSetupFinished,
			EnableReadinessCheck: true,
		}); err != nil {
			setupLog.Error(err, "unable to setup cert rotation")
			os.Exit(1)
		}
	} else {
		setupLog.Info("Webhooks disabled, skipping cert rotation setup")
		close(certSetupFinished)
	}

	ctx := ctrl.SetupSignalHandler()

	tracker := management.InitTracker(ctx, internalConfig)
	scheduler, err := scheduling.NewReconciler(mgr.GetClient(), tracker)
	if err != nil {
		setupLog.Error(err, "unable to create scheduler reconciler")
		os.Exit(1)
	}
	reconciler, err := controller.NewCacheReconciler(
		mgr.GetClient(),
		mgr.GetScheme(),
		mgr.GetEventRecorder("cache-controller"),
		scaling.New(
			mgr.GetClient(),
			mgr.GetScheme(),
			internalConfig,
		),
		scheduler,
	)
	if err != nil {
		setupLog.Error(err, "unable to create reconciler", "controller", "Cache")
		os.Exit(1)
	}
	if err := reconciler.SetupWithManager(ctx, mgr); err != nil {
		setupLog.Error(err, "unable to create controller", "controller", "Cache")
		os.Exit(1)
	}

	// Setup webhooks after cert rotation is ready
	if enableWebhooks {
		go func() {
			<-certSetupFinished
			setupLog.Info("Certificates ready, registering webhooks")

			if err := (&storagev1.Cache{}).SetupWebhookWithManager(mgr); err != nil {
				setupLog.Error(err, "unable to create webhook", "webhook", "Cache")
				os.Exit(1)
			}
			setupLog.Info("Webhooks registered successfully")
		}()
	}

	// +kubebuilder:scaffold:builder

	// Setup health checks with cert readiness awareness
	checker := healthz.Ping
	if enableWebhooks {
		checker = func(req *http.Request) error {
			select {
			case <-certSetupFinished:
				return mgr.GetWebhookServer().StartedChecker()(req)
			default:
				return fmt.Errorf("certs are not ready yet")
			}
		}
	}

	if err := mgr.AddHealthzCheck("healthz", checker); err != nil {
		setupLog.Error(err, "unable to set up health check")
		os.Exit(1)
	}
	if err := mgr.AddReadyzCheck("readyz", checker); err != nil {
		setupLog.Error(err, "unable to set up ready check")
		os.Exit(1)
	}

	setupLog.Info("starting manager")
	if err := mgr.Start(ctx); err != nil {
		setupLog.Error(err, "problem running manager")
		os.Exit(1)
	}
}
