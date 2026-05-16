#!/usr/bin/env python3
import os
import sys


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    return 2


def import_kconfiglib():
    try:
        import kconfiglib
    except ModuleNotFoundError:
        print(
            "error: Kconfiglib is missing. Run './kernel/build/get-deps' "
            "or 'make menuconfig' from the repository root.",
            file=sys.stderr,
        )
        raise SystemExit(2)

    return kconfiglib


def touch(path):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "a", encoding="utf-8"):
        pass
    os.utime(path, None)


def load_kconfig(kconfiglib, kconfig_path, config_path, load_config):
    os.environ["KCONFIG_CONFIG"] = config_path
    os.environ.setdefault("srctree", os.getcwd())

    kconf = kconfiglib.Kconfig(kconfig_path, warn_to_stderr=True)
    if load_config and os.path.exists(config_path):
        kconf.load_config(config_path)
    return kconf


def write_outputs(kconf, config_path, header_path, write_config):
    if write_config:
        kconf.write_config(config_path)
        touch(config_path)

    kconf.write_autoconf(header_path)
    touch(header_path)


def main(argv):
    if len(argv) != 5:
        return die("usage: kconfig.py <defconfig|sync> Kconfig .config autoconf.h")

    mode, kconfig_path, config_path, header_path = argv[1:]
    kconfiglib = import_kconfiglib()

    if mode == "defconfig":
        kconf = load_kconfig(kconfiglib, kconfig_path, config_path, load_config=False)
        write_outputs(kconf, config_path, header_path, write_config=True)
        return 0

    if mode == "sync":
        kconf = load_kconfig(kconfiglib, kconfig_path, config_path, load_config=True)
        write_outputs(kconf, config_path, header_path, write_config=True)
        return 0

    return die(f"unknown mode: {mode}")


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
