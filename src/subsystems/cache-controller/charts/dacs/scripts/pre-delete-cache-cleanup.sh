#!/bin/sh
# Copyright (c) Microsoft Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -eu

force_delete_caches=false
timeout_seconds=300
while [ "$#" -gt 0 ]; do
  case "$1" in
    --force-cache-deletion)
      force_delete_caches=true
      shift
      ;;
    --timeout-seconds)
      timeout_seconds=${2:?"--timeout-seconds requires a value"}
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done
case "$timeout_seconds" in
  *[!0-9]*|0[0-9]*)
    echo "--timeout-seconds must be a non-negative integer without leading zeros." >&2
    exit 1
    ;;
esac

crd=$(kubectl get crd caches.storage.azure.com --ignore-not-found --output=name)
if [ -z "$crd" ]; then
  echo "Cache CRD not found, nothing to clean up."
  exit 0
fi

if [ "$force_delete_caches" = false ]; then
  caches=$(kubectl get caches.storage.azure.com --all-namespaces --output=name)
  if [ -n "$caches" ]; then
    echo "Cannot uninstall the controller while Cache resources remain:" >&2
    printf '%s\n' "$caches" >&2
    echo "Delete Cache resources and wait for cleanup, then retry uninstall." >&2
    exit 1
  fi
  echo "No Cache resources remain; uninstall can proceed."
  exit 0
fi

deadline=$(($(date +%s) + timeout_seconds))
kubectl delete caches.storage.azure.com --all-namespaces --all \
  --wait=false \
  --ignore-not-found=true
while :; do
  caches=$(kubectl get caches.storage.azure.com --all-namespaces --output=name)
  if [ -z "$caches" ]; then
    echo "All Cache resources deleted."
    exit 0
  fi
  if [ "$(date +%s)" -ge "$deadline" ]; then
    break
  fi
  sleep 5
done

echo "Cache cleanup timed out; removing finalizers from remaining terminating Cache resources." >&2
caches=$(kubectl get caches.storage.azure.com --all-namespaces \
  --output='jsonpath={range .items[?(@.metadata.deletionTimestamp)]}{.metadata.namespace}{" "}{.metadata.name}{" "}{.metadata.uid}{"\n"}{end}')
printf '%s\n' "$caches" | while read -r namespace name uid; do
  if [ -z "$name" ]; then
    continue
  fi
  if ! kubectl patch caches.storage.azure.com "$name" --namespace "$namespace" \
    --type=json \
    --patch "[{\"op\":\"test\",\"path\":\"/metadata/uid\",\"value\":\"$uid\"},{\"op\":\"add\",\"path\":\"/metadata/finalizers\",\"value\":[]}]"; then
    remaining=$(kubectl get caches.storage.azure.com "$name" --namespace "$namespace" \
      --ignore-not-found --output=name)
    if [ -n "$remaining" ]; then
      echo "Failed to remove finalizers from Cache $namespace/$name." >&2
      exit 1
    fi
  fi
done
caches=$(kubectl get caches.storage.azure.com --all-namespaces --output=name)
if [ -n "$caches" ]; then
  echo "Cache resources still remain after force deletion; refusing to uninstall:" >&2
  printf '%s\n' "$caches" >&2
  exit 1
fi
echo "All Cache resources deleted."
