import os
import subprocess
import sys


def run_as_root():
    if os.geteuid() != 0:
        print("Please run as root")
        sys.exit(1)


def install_dependencies():
    subprocess.run(["apt-get", "update"], check=True)
    subprocess.run(
        [
            "apt",
            "install",
            "-y",
            "--no-install-recommends",
            "libssl-dev",
            "libcurl4-openssl-dev",
            "curl",
            "libjsoncpp-dev",
            "uuid-dev",
            "ibverbs-providers",
        ],
        check=True,
    )

    try:
        os.symlink("/usr/lib/x86_64-linux-gnu/libjsoncpp.so.1.9.5", "/usr/lib/x86_64-linux-gnu/libjsoncpp.so.1")
    except FileExistsError:
        pass
    subprocess.run(
        ["wget", "https://nz2.archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb"],
        check=True,
    )
    subprocess.run(["dpkg", "-i", "libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb"], check=True)


def install_python_packages():
    subprocess.run([sys.executable, "-m", "pip", "install", "requests"], check=False)


def main():
    run_as_root()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"Working directory: {script_dir}")

    print("Installing storage interception dependencies")
    install_dependencies()
    print("Installed storage interception dependencies")

    print("Installing python dependent packages")
    install_python_packages()
    print("Installed python dependent packages")


if __name__ == "__main__":
    main()
