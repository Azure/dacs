#!/usr/bin/env python3

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

import argparse
import hashlib
import json
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path

import h5py
import numpy as np


PRIMARY_FILE_SIZE = 324_089
BOUNDARY_FILE_SIZE = 5 * 1024 * 1024
SMALL_FILE_COUNT = 20
LARGE_FILE_COUNT = 20
STORAGE_INTERCEPT_TEST_FILE_SIZE = 16 * 1024 * 1024


def write_deterministic_file(path: Path, size: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pattern = hashlib.sha256(path.as_posix().encode()).digest()
    with path.open("wb") as output:
        remaining = size
        while remaining:
            chunk = pattern[: min(remaining, len(pattern))]
            output.write(chunk)
            remaining -= len(chunk)


def create_hdf5_file(path: Path, size: int, index: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data_size = max(size - 4096, 1024)
    pattern = np.arange(data_size, dtype=np.uint8)
    pattern ^= index
    with h5py.File(path, "w") as output:
        output.create_dataset("data", data=pattern)


def create_archive(source: Path, destination: Path) -> None:
    with tarfile.open(destination.with_suffix(".tar.gz"), "w:gz") as archive:
        archive.add(source, arcname=source.name)
    with zipfile.ZipFile(
        destination.with_suffix(".zip"), "w", compression=zipfile.ZIP_DEFLATED
    ) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source.parent))


def manifest_entry(path: str, size: int) -> str:
    return f"{path},{size}"


def write_manifest(path: Path, entries: list[str]) -> None:
    path.write_text(f"{len(entries)}\n" + "\n".join(entries) + "\n")


def run_az(*args: str) -> str:
    if shutil.which("az") is None:
        raise RuntimeError("Azure CLI is required when --upload is specified")

    result = subprocess.run(
        ["az", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def fixture_inventory(container_dir: Path) -> dict[str, int]:
    return {
        path.relative_to(container_dir).as_posix(): path.stat().st_size
        for path in container_dir.rglob("*")
        if path.is_file()
    }


def verify_uploaded_fixtures(
    container_dir: Path, account_name: str, container_name: str
) -> None:
    expected = fixture_inventory(container_dir)
    actual: dict[str, int] = {}
    prefixes = sorted({name.partition("/")[0] for name in expected})

    for prefix in prefixes:
        blobs = json.loads(
            run_az(
                "storage",
                "blob",
                "list",
                "--account-name",
                account_name,
                "--container-name",
                container_name,
                "--prefix",
                prefix,
                "--auth-mode",
                "login",
                "--query",
                "[].{name:name,size:properties.contentLength}",
                "--only-show-errors",
                "--output",
                "json",
            )
        )
        actual.update({blob["name"]: int(blob["size"]) for blob in blobs})

    missing = sorted(name for name in expected if name not in actual)
    mismatched = sorted(
        (name, expected[name], actual[name])
        for name in expected.keys() & actual.keys()
        if expected[name] != actual[name]
    )
    if missing or mismatched:
        details = []
        if missing:
            details.append(f"missing blobs: {', '.join(missing)}")
        if mismatched:
            details.append(
                "size mismatches: "
                + ", ".join(
                    f"{name} expected {expected_size}, got {actual_size}"
                    for name, expected_size, actual_size in mismatched
                )
            )
        raise RuntimeError(
            f"Fixture verification failed for container {container_name}: "
            + "; ".join(details)
        )

    print(f"Verified {len(expected)} fixtures in container {container_name}")


def upload_fixtures(output_dir: Path, account_name: str) -> None:
    for container_name in ("dataset", "dataset2"):
        run_az(
            "storage",
            "container",
            "create",
            "--account-name",
            account_name,
            "--name",
            container_name,
            "--auth-mode",
            "login",
            "--only-show-errors",
            "--output",
            "none",
        )
        run_az(
            "storage",
            "blob",
            "upload-batch",
            "--account-name",
            account_name,
            "--destination",
            container_name,
            "--source",
            str(output_dir / container_name),
            "--overwrite",
            "true",
            "--auth-mode",
            "login",
            "--only-show-errors",
            "--output",
            "none",
        )
        verify_uploaded_fixtures(
            output_dir / container_name, account_name, container_name
        )

    print(f"Uploaded Azure test fixtures to storage account {account_name}")


def generate_fixtures(
    output_dir: Path,
    manifest_dir: Path,
    unit_manifest: Path,
    directory_manifest: Path,
    account_name: str,
) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)

    dataset = output_dir / "dataset"
    dataset2 = output_dir / "dataset2"
    class_a = dataset / "synthetic/small/class-a"
    class_b = dataset / "synthetic/small/class-b"

    write_deterministic_file(
        dataset / "testFileForUt", STORAGE_INTERCEPT_TEST_FILE_SIZE
    )
    write_deterministic_file(dataset / "testDirForUt/file-1", 0)
    write_deterministic_file(dataset / "testDirForUt/file-2", 0)
    write_deterministic_file(dataset / "testDirForUt/dir-1/fixture", 0)

    small_files: list[Path] = []
    for index in range(SMALL_FILE_COUNT // 2):
        path = class_a / f"fixture-{index:03}.bin"
        write_deterministic_file(path, 16 * 1024 + index * 4096)
        small_files.append(path)

    for index in range(SMALL_FILE_COUNT // 2):
        name = "primary.bin" if index == 0 else f"fixture-{index + 10:03}.bin"
        size = PRIMARY_FILE_SIZE if index == 0 else 32 * 1024 + index * 8192
        if index == 1:
            size = BOUNDARY_FILE_SIZE
        path = class_b / name
        write_deterministic_file(path, size)
        small_files.append(path)

    write_deterministic_file(class_b / "nested-a/marker.bin", 64)
    write_deterministic_file(class_b / "nested-b/marker.bin", 64)
    create_archive(class_a, dataset / "synthetic/small")

    large_files: list[Path] = []
    large_dir = dataset / "synthetic/large"
    for index in range(LARGE_FILE_COUNT):
        size = BOUNDARY_FILE_SIZE if index == 0 else 64 * 1024 + index * 8192
        path = large_dir / f"fixture-{index:03}.hdf5"
        create_hdf5_file(path, size, index)
        large_files.append(path)
    create_archive(large_dir, dataset / "synthetic/large")

    shutil.copytree(dataset / "synthetic/large", dataset2 / "synthetic/large")
    shutil.copy2(
        dataset / "synthetic/large.tar.gz", dataset2 / "synthetic/large.tar.gz"
    )
    shutil.copy2(dataset / "synthetic/large.zip", dataset2 / "synthetic/large.zip")

    write_deterministic_file(
        dataset / "client-integration/rust-fixture/sample/sample.bin", 4096
    )

    small_entries = [
        manifest_entry(
            f"/mnt/quiverfs/{path.relative_to(dataset).as_posix()}",
            path.stat().st_size,
        )
        for path in small_files
    ]
    unit_entries = [
        manifest_entry(
            f"/mnt/dataPathIntcpt/{path.relative_to(dataset).as_posix()}",
            path.stat().st_size,
        )
        for path in small_files
    ]
    large_entries = [
        manifest_entry(
            f"/mnt/quiverfs/{path.relative_to(dataset).as_posix()}",
            path.stat().st_size,
        )
        for path in large_files
    ]
    dynamic_account_entries = [
        manifest_entry(
            f"/mnt/quiverfs/{account_name}.blob.core.windows.net/dataset/"
            f"{path.relative_to(dataset).as_posix()}",
            path.stat().st_size,
        )
        for path in large_files
    ]

    dynamic_container_entries = []
    for index, path in enumerate(large_files):
        relative_path = path.relative_to(dataset).as_posix()
        if index < 7:
            prefix = "/mnt/quiverfs/dataset"
        elif index < 14:
            prefix = "/mnt/quiverfs/dataset2"
        else:
            prefix = "/mnt/quiverfs2/dataset"
        dynamic_container_entries.append(
            manifest_entry(f"{prefix}/{relative_path}", path.stat().st_size)
        )

    write_manifest(manifest_dir / "small_dataset_testcases.txt", small_entries)
    write_manifest(manifest_dir / "large_dataset_testcases.txt", large_entries)
    write_manifest(
        manifest_dir / "large_dataset_testcases_dynamic_account.txt",
        dynamic_account_entries,
    )
    write_manifest(
        manifest_dir / "large_dataset_testcases_dynamic_container.txt",
        dynamic_container_entries,
    )
    unit_manifest.write_text("\n".join(unit_entries) + "\n")
    directory_manifest.write_text(
        "\n".join(
            "/mnt/dataPathIntcpt/synthetic/small/class-b,10"
            for _ in range(SMALL_FILE_COUNT)
        )
        + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate deterministic synthetic fixtures for Azure storage tests."
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--manifest-dir", type=Path, required=True)
    parser.add_argument("--unit-manifest", type=Path, required=True)
    parser.add_argument("--directory-manifest", type=Path, required=True)
    parser.add_argument("--account-name", default="dacstest")
    parser.add_argument(
        "--upload",
        action="store_true",
        help="Create the dataset containers and upload all generated fixtures.",
    )
    args = parser.parse_args()

    args.manifest_dir.mkdir(parents=True, exist_ok=True)
    args.unit_manifest.parent.mkdir(parents=True, exist_ok=True)
    args.directory_manifest.parent.mkdir(parents=True, exist_ok=True)
    generate_fixtures(
        args.output_dir,
        args.manifest_dir,
        args.unit_manifest,
        args.directory_manifest,
        args.account_name,
    )
    if args.upload:
        upload_fixtures(args.output_dir, args.account_name)


if __name__ == "__main__":
    main()
