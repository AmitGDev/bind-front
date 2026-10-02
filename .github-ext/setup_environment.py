"""Project-specific external dependency setup.

Installs GCC 15 on Linux to provide the required libstdc++ 15.
The script safely skips installation on other platforms.
Note: Ubuntu 24.04's default repositories do not contain gcc-15;
adding ppa:ubuntu-toolchain-r/test before installation resolves this.
"""

from __future__ import annotations

import platform
import shutil
import subprocess
import sys

GCC_VERSION = "15"


def _run(command: list[str]) -> None:
    print(f"Running: {' '.join(command)}")
    subprocess.run(command, check=True)


def install_gcc() -> None:
    """Install GCC 15 if it is not already installed."""

    gcc_name = f"gcc-{GCC_VERSION}"
    gxx_name = f"g++-{GCC_VERSION}"

    gcc = shutil.which(gcc_name)
    gxx = shutil.which(gxx_name)

    if not gcc or not gxx:
        _run(["sudo", "apt-get", "update"])

        _run(
            [
                "sudo",
                "apt-get",
                "install",
                "-y",
                "software-properties-common",
            ]
        )

        _run(
            [
                "sudo",
                "add-apt-repository",
                "-y",
                "ppa:ubuntu-toolchain-r/test",
            ]
        )

        _run(["sudo", "apt-get", "update"])

        _run(
            [
                "sudo",
                "apt-get",
                "install",
                "-y",
                gcc_name,
                gxx_name,
            ]
        )

        gcc = shutil.which(gcc_name)
        gxx = shutil.which(gxx_name)

    if not gcc or not gxx:
        raise RuntimeError("GCC 15 installation was not successful.")

    _run([gcc, "--version"])
    _run([gxx, "--version"])

    print(f"GCC compiler: {gcc}")
    print(f"G++ compiler: {gxx}")


def main() -> int:
    if platform.system() != "Linux":
        print(f"Skipping GCC {GCC_VERSION} installation on {platform.system()}.")
        return 0

    install_gcc()
    return 0


if __name__ == "__main__":
    sys.exit(main())
