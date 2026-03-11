/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Cache eviction by group ID using the native cache server protobuf protocol
# --------------------------------------------------------------------------------------------------------------------
*/
// Regenerate cachepb/cache.pb.go after changes to src/subsystems/storage/common/proto/cache.proto
//go:generate protoc --proto_path=../../../src/subsystems/storage/common/proto --go_out=cachepb --go_opt=Mcache.proto=github.com/Azure/dacs/test/pkg/scenario/cachepb --go_opt=paths=source_relative cache.proto

package scenario

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os/exec"
	"strings"
	"time"

	"github.com/Azure/dacs/test/pkg/scenario/cachepb"
	"google.golang.org/protobuf/proto"
)

const (
	cacheServerPort = "9065"
	connectTimeout  = 10 * time.Second
	readTimeout     = 30 * time.Second
)

// EvictCacheGroup evicts all cached files belonging to the given group ID from
// all cache servers discovered via the discovery service.
// It uses kubectl port-forward to reach the in-cluster discovery service.
func EvictCacheGroup(ctx context.Context, discoveryURL string, groupID string) error {
	if discoveryURL == "" {
		return fmt.Errorf("discovery URL is empty")
	}
	if groupID == "" {
		return fmt.Errorf("group ID is empty")
	}

	// Use kubectl port-forward to reach the discovery service
	log.Info("Setting up port-forward to discovery service", "discoveryURL", discoveryURL)

	// Parse namespace from discoveryURL (format: svc-name.namespace.svc.cluster.local)
	parts := strings.Split(discoveryURL, ".")
	if len(parts) < 2 {
		return fmt.Errorf("invalid discovery URL format: %s", discoveryURL)
	}
	svcName := parts[0]
	namespace := parts[1]

	// Find a free local port
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("find free port: %w", err)
	}
	localPort := listener.Addr().(*net.TCPAddr).Port
	listener.Close()

	// Start port-forward
	pfCtx, pfCancel := context.WithCancel(ctx)
	defer pfCancel()

	var pfStderr strings.Builder
	cmd := exec.CommandContext(pfCtx, "kubectl", "port-forward",
		fmt.Sprintf("svc/%s", svcName),
		fmt.Sprintf("%d:%s", localPort, cacheServerPort),
		"-n", namespace)
	cmd.Stdout = io.Discard
	cmd.Stderr = &pfStderr
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start port-forward: %w", err)
	}
	defer func() {
		pfCancel()
		cmd.Wait()
		if errOutput := pfStderr.String(); errOutput != "" {
			log.Info("Port-forward stderr (discovery)", "output", errOutput)
		}
	}()

	// Wait for port-forward to be ready by polling the local port
	localAddr := fmt.Sprintf("127.0.0.1:%d", localPort)
	if err := waitForPort(localAddr, 10*time.Second); err != nil {
		return fmt.Errorf("port-forward not ready: %w", err)
	}
	log.Info("Discovering cache servers via port-forward", "localAddr", localAddr)

	servers, err := discoverCacheServers(localAddr)
	if err != nil {
		return fmt.Errorf("failed to discover cache servers: %w", err)
	}
	if len(servers) == 0 {
		return fmt.Errorf("no cache servers found via discovery service")
	}
	log.Info("Found cache servers", "count", len(servers), "servers", servers)

	// The returned server addresses are internal K8s hostnames.
	// Port-forward to each cache server to send delete requests.
	var errs []error
	for _, server := range servers {
		if err := evictViaPortForward(ctx, server, namespace, groupID); err != nil {
			errs = append(errs, err)
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("eviction errors: %v", errs)
	}
	return nil
}

// evictViaPortForward sets up a port-forward to a specific cache server pod and sends
// the delete request.
func evictViaPortForward(ctx context.Context, serverAddr string, namespace string, groupID string) error {
	// serverAddr is like "cacheserver-0.cacheserver.cache-server.svc.cluster.local"
	// Extract pod name (first component)
	parts := strings.Split(serverAddr, ".")
	podName := parts[0]

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fmt.Errorf("find free port: %w", err)
	}
	localPort := listener.Addr().(*net.TCPAddr).Port
	listener.Close()

	pfCtx, pfCancel := context.WithCancel(ctx)
	defer pfCancel()

	var pfStderr strings.Builder
	cmd := exec.CommandContext(pfCtx, "kubectl", "port-forward",
		fmt.Sprintf("pod/%s", podName),
		fmt.Sprintf("%d:%s", localPort, cacheServerPort),
		"-n", namespace)
	cmd.Stdout = io.Discard
	cmd.Stderr = &pfStderr
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start port-forward to %s: %w", podName, err)
	}
	defer func() {
		pfCancel()
		cmd.Wait()
		if errOutput := pfStderr.String(); errOutput != "" {
			log.Info("Port-forward stderr", "pod", podName, "output", errOutput)
		}
	}()

	localAddr := fmt.Sprintf("127.0.0.1:%d", localPort)
	if err := waitForPort(localAddr, 10*time.Second); err != nil {
		return fmt.Errorf("port-forward to %s not ready: %w", podName, err)
	}
	if err := sendDeleteByGroupID(localAddr, groupID); err != nil {
		return fmt.Errorf("evict from %s: %w", podName, err)
	}
	log.Info("Evicted cache group", "server", podName, "groupID", groupID)
	return nil
}

// discoverCacheServers connects to the discovery service and returns the list
// of cache server addresses. addr should be host:port.
func discoverCacheServers(addr string) ([]string, error) {
	conn, err := net.DialTimeout("tcp", addr, connectTimeout)
	if err != nil {
		return nil, fmt.Errorf("connect to discovery %s: %w", addr, err)
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(readTimeout))

	// Build GetCacheServersRequest
	req := &cachepb.Request{
		Type: &cachepb.Request_Getcacheserversrequest{
			Getcacheserversrequest: &cachepb.GetCacheServersRequest{
				Requestid: "scenario-evict-discover",
			},
		},
	}

	if err := sendProto(conn, req); err != nil {
		return nil, fmt.Errorf("send GetCacheServers: %w", err)
	}

	respBytes, err := recvProto(conn)
	if err != nil {
		return nil, fmt.Errorf("recv GetCacheServers: %w", err)
	}

	var resp cachepb.GetCacheServersResponse
	if err := proto.Unmarshal(respBytes, &resp); err != nil {
		return nil, fmt.Errorf("unmarshal GetCacheServersResponse: %w", err)
	}

	if resp.GetResult() != cachepb.GetCacheServersResponse_SUCCESS {
		return nil, fmt.Errorf("GetCacheServers returned %s", resp.GetResult().String())
	}

	return resp.GetServeraddresses(), nil
}

// sendDeleteByGroupID sends a DeleteRequest with groupid to a cache server.
func sendDeleteByGroupID(addr string, groupID string) error {
	conn, err := net.DialTimeout("tcp", addr, connectTimeout)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(readTimeout))

	req := &cachepb.Request{
		Type: &cachepb.Request_Deleterequest{
			Deleterequest: &cachepb.DeleteRequest{
				Requestid: "scenario-evict-" + groupID,
				Type: &cachepb.DeleteRequest_Groupid{
					Groupid: []byte(groupID),
				},
			},
		},
	}

	if err := sendProto(conn, req); err != nil {
		return fmt.Errorf("send DeleteRequest: %w", err)
	}

	respBytes, err := recvProto(conn)
	if err != nil {
		return fmt.Errorf("recv DeleteResponse: %w", err)
	}

	var resp cachepb.DeleteResponse
	if err := proto.Unmarshal(respBytes, &resp); err != nil {
		return fmt.Errorf("unmarshal DeleteResponse: %w", err)
	}

	if resp.GetResult() != cachepb.DeleteResponse_SUCCESS {
		return fmt.Errorf("delete returned %s", resp.GetResult().String())
	}

	return nil
}

// waitForPort polls a TCP address until it accepts connections or the timeout expires.
func waitForPort(addr string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		conn, err := net.DialTimeout("tcp", addr, 500*time.Millisecond)
		if err == nil {
			conn.Close()
			return nil
		}
		time.Sleep(200 * time.Millisecond)
	}
	return fmt.Errorf("timeout waiting for %s", addr)
}
func sendProto(conn net.Conn, msg proto.Message) error {
	data, err := proto.Marshal(msg)
	if err != nil {
		return fmt.Errorf("marshal: %w", err)
	}

	// 4-byte big-endian length prefix (network byte order, matching C++ ConvertIntToBytes)
	lenBuf := make([]byte, 4)
	binary.BigEndian.PutUint32(lenBuf, uint32(len(data)))

	if _, err := conn.Write(lenBuf); err != nil {
		return fmt.Errorf("write length: %w", err)
	}
	if _, err := conn.Write(data); err != nil {
		return fmt.Errorf("write data: %w", err)
	}
	return nil
}

// recvProto reads a 4-byte length prefix followed by that many bytes of protobuf data.
func recvProto(conn net.Conn) ([]byte, error) {
	lenBuf := make([]byte, 4)
	if _, err := io.ReadFull(conn, lenBuf); err != nil {
		return nil, fmt.Errorf("read length: %w", err)
	}

	msgLen := binary.BigEndian.Uint32(lenBuf)
	if msgLen > 10*1024*1024 { // 10MB sanity limit
		return nil, fmt.Errorf("message too large: %d bytes", msgLen)
	}

	data := make([]byte, msgLen)
	if _, err := io.ReadFull(conn, data); err != nil {
		return nil, fmt.Errorf("read data: %w", err)
	}
	return data, nil
}
