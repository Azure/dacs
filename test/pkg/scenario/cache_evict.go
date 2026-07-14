/*
--------------------------------------------------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.
# Licensed under the Apache License, Version 2.0.
# Cache management using the native cache server protobuf protocol
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

// sendDeleteByLastAccessTime sends a DeleteRequest with lastaccesstime to flush all data
// older than the given time from a cache server.
func sendDeleteByLastAccessTime(addr string, timestamp string) error {
	conn, err := net.DialTimeout("tcp", addr, connectTimeout)
	if err != nil {
		return fmt.Errorf("connect: %w", err)
	}
	defer conn.Close()
	conn.SetDeadline(time.Now().Add(readTimeout))

	req := &cachepb.Request{
		Type: &cachepb.Request_Deleterequest{
			Deleterequest: &cachepb.DeleteRequest{
				Requestid: "scenario-flush-" + timestamp,
				Type: &cachepb.DeleteRequest_Lastaccesstime{
					Lastaccesstime: []byte(timestamp),
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

// FlushCache evicts ALL cached data from all cache servers by sending a
// lastaccesstime delete with a far-future timestamp.
func FlushCache(ctx context.Context, discoveryURL string) error {
	if discoveryURL == "" {
		return fmt.Errorf("discovery URL is empty")
	}

	log.Info("Flushing all cache data", "discoveryURL", discoveryURL)

	parts := strings.Split(discoveryURL, ".")
	if len(parts) < 2 {
		return fmt.Errorf("invalid discovery URL format: %s", discoveryURL)
	}
	svcName := parts[0]
	namespace := parts[1]

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
	}()

	localAddr := fmt.Sprintf("127.0.0.1:%d", localPort)
	if err := waitForPort(localAddr, 10*time.Second); err != nil {
		return fmt.Errorf("port-forward not ready: %w", err)
	}

	servers, err := discoverCacheServers(localAddr)
	if err != nil {
		return fmt.Errorf("failed to discover cache servers: %w", err)
	}
	log.Info("Flushing cache servers", "count", len(servers))

	// Use a far-future timestamp to evict everything
	futureTime := "2099-12-31T23:59:59"
	var errs []error
	for _, server := range servers {
		serverParts := strings.Split(server, ".")
		podName := serverParts[0]

		pfListener, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			errs = append(errs, fmt.Errorf("find free port for %s: %w", podName, err))
			continue
		}
		podPort := pfListener.Addr().(*net.TCPAddr).Port
		pfListener.Close()

		podPfCtx, podPfCancel := context.WithCancel(ctx)
		podCmd := exec.CommandContext(podPfCtx, "kubectl", "port-forward",
			fmt.Sprintf("pod/%s", podName),
			fmt.Sprintf("%d:%s", podPort, cacheServerPort),
			"-n", namespace)
		podCmd.Stdout = io.Discard
		podCmd.Stderr = io.Discard
		if err := podCmd.Start(); err != nil {
			podPfCancel()
			errs = append(errs, fmt.Errorf("start port-forward to %s: %w", podName, err))
			continue
		}

		podAddr := fmt.Sprintf("127.0.0.1:%d", podPort)
		if err := waitForPort(podAddr, 10*time.Second); err != nil {
			podPfCancel()
			podCmd.Wait()
			errs = append(errs, fmt.Errorf("port-forward to %s not ready: %w", podName, err))
			continue
		}
		if err := sendDeleteByLastAccessTime(podAddr, futureTime); err != nil {
			errs = append(errs, fmt.Errorf("flush %s: %w", podName, err))
		} else {
			log.Info("Flushed cache server", "server", podName)
		}
		podPfCancel()
		podCmd.Wait()
	}

	if len(errs) > 0 {
		return fmt.Errorf("flush errors: %v", errs)
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
