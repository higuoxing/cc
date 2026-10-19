#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
VMTEST=${VMTEST:-vmtest}
QEMU_STATE_DIR=${QEMU_STATE_DIR:-$SCRIPT_DIR/.qemu}
QEMU_IMAGE_URL=${QEMU_IMAGE_URL:-https://cloud.debian.org/images/cloud/trixie/latest/debian-13-genericcloud-amd64.qcow2}
QEMU_IMAGE_NAME=${QEMU_IMAGE_NAME:-debian-13-genericcloud-amd64.qcow2}
QEMU_DISK_FORMAT=${QEMU_DISK_FORMAT:-qcow2}
QEMU_MEMORY=${QEMU_MEMORY:-2048}
QEMU_CPUS=${QEMU_CPUS:-2}
QEMU_TIMEOUT=${QEMU_TIMEOUT:-900}

for tool in "$QEMU" "$VMTEST" qemu-img hdiutil; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required executable not found: %s\n' "$tool" >&2
        printf 'Install QEMU with brew install qemu, and vmtest with cargo install vmtest.\n' >&2
        exit 2
    fi
done
QEMU=$(command -v "$QEMU")
if [[ ! "$QEMU_CPUS" =~ ^[1-9][0-9]*$ || ! "$QEMU_TIMEOUT" =~ ^[1-9][0-9]*$ ]]; then
    printf 'QEMU_CPUS and QEMU_TIMEOUT must be positive integers.\n' >&2
    exit 2
fi
if ((QEMU_CPUS > 255)); then
    printf 'QEMU_CPUS must be at most 255 (vmtest limit).\n' >&2
    exit 2
fi

mkdir -p "$QEMU_STATE_DIR"
QEMU_STATE_DIR=$(cd -- "$QEMU_STATE_DIR" && pwd)
QEMU_DISK=${QEMU_DISK:-$QEMU_STATE_DIR/$QEMU_IMAGE_NAME}
GUEST_DISK="$QEMU_STATE_DIR/vmtest-amd64.qcow2"
READY_FILE="$GUEST_DISK.ready"
LOCK_DIR="$GUEST_DISK.lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    printf 'Another test-qemu run holds %s (remove it if a previous run was interrupted).\n' "$LOCK_DIR" >&2
    exit 2
fi
if ! WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/test-qemu.XXXXXX"); then
    rmdir "$LOCK_DIR"
    exit 1
fi
QEMU_PID=

cleanup() {
    local exit_status=$?
    if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi
    rmdir "$LOCK_DIR" 2>/dev/null || true
    if [[ "${TEST_KEEP_TMP:-0}" == 1 || "$exit_status" != 0 ]]; then
        printf 'QEMU logs and vmtest configuration kept in %s\n' "$WORK_DIR" >&2
    else
        rm -rf -- "$WORK_DIR"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ ! -f "$QEMU_DISK" ]]; then
    if [[ "$QEMU_DISK" != "${QEMU_STATE_DIR}/$QEMU_IMAGE_NAME" ]]; then
        printf 'QEMU_DISK does not exist: %s\n' "$QEMU_DISK" >&2
        exit 2
    fi
    CHECKSUM_URL="${QEMU_IMAGE_URL%/*}/SHA512SUMS"
    CHECKSUM_FILE="$QEMU_STATE_DIR/SHA512SUMS"
    DOWNLOAD_FILE="$QEMU_DISK.download"
    printf 'Downloading Debian x86-64 cloud image...\n'
    curl -fL --retry 3 "$QEMU_IMAGE_URL" -o "$DOWNLOAD_FILE"
    curl -fsSL --retry 3 "$CHECKSUM_URL" -o "$CHECKSUM_FILE"
    EXPECTED_SHA=$(awk -v name="$QEMU_IMAGE_NAME" '$2 == name || $2 == "./" name { print $1; exit }' "$CHECKSUM_FILE")
    if [[ ! "$EXPECTED_SHA" =~ ^[[:xdigit:]]{128}$ ]]; then
        printf 'Could not find a SHA-512 checksum for %s in %s\n' "$QEMU_IMAGE_NAME" "$CHECKSUM_FILE" >&2
        exit 1
    fi
    ACTUAL_SHA=$(shasum -a 512 "$DOWNLOAD_FILE" | awk '{ print $1 }')
    if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
        printf 'SHA-512 checksum mismatch for downloaded Debian image\n' >&2
        exit 1
    fi
    mv "$DOWNLOAD_FILE" "$QEMU_DISK"
fi
BASE_DISK=$(cd -- "$(dirname -- "$QEMU_DISK")" && printf '%s/%s' "$PWD" "$(basename -- "$QEMU_DISK")")
CACHE_KEY="1|$BASE_DISK|$(stat -f '%i:%z:%m' "$BASE_DISK")"

if [[ ! -f "$GUEST_DISK" || ! -f "$READY_FILE" || "$(cat "$READY_FILE")" != "$CACHE_KEY" ]]; then
    PROVISION_DISK="$WORK_DIR/guest.qcow2"
    SERIAL_LOG="$WORK_DIR/provision.log"
    SEED_DIR="$WORK_DIR/seed"
    SEED_ISO="$WORK_DIR/seed.iso"
    mkdir "$SEED_DIR"
    qemu-img create -q -f qcow2 -F "$QEMU_DISK_FORMAT" -b "$BASE_DISK" "$PROVISION_DISK"
    qemu-img resize -q "$PROVISION_DISK" 8G

    # genericcloud omits 9p. Install the full kernel and select it explicitly:
    # GRUB may otherwise keep preferring the installed cloud kernel.
    cat > "$SEED_DIR/user-data" <<'EOF'
#cloud-config
ssh_pwauth: false
disable_root: true
package_update: true
packages:
  - linux-image-amd64
  - build-essential
  - qemu-guest-agent
write_files:
  - path: /etc/systemd/network/10-enp0s2.network
    permissions: '0644'
    content: |
      [Match]
      Name=e*

      [Network]
      DHCP=ipv4
runcmd:
  - |
    set -eu
    command -v gcc
    command -v make
    command -v qemu-ga
    kernel=$(find /boot -name 'vmlinuz-*-amd64' ! -name '*-cloud-amd64' | sort -V | tail -n 1)
    test -n "$kernel"
    kernel=${kernel#/boot/vmlinuz-}
    modinfo -k "$kernel" 9p >/dev/null
    modinfo -k "$kernel" 9pnet_virtio >/dev/null
    printf 'GRUB_DEFAULT="Advanced options for Debian GNU/Linux>Debian GNU/Linux, with Linux %s"\n' "$kernel" > /etc/default/grub.d/99-cc-vmtest.cfg
    update-grub
    systemctl enable --now qemu-guest-agent
    touch /etc/cloud/cloud-init.disabled
    sync
    echo CC_VMTEST_READY
power_state:
  delay: now
  mode: poweroff
  timeout: 30
  condition: true
EOF
    cat > "$SEED_DIR/meta-data" <<EOF
instance-id: cc-vmtest-$(date +%s)-$$
local-hostname: cc-test
EOF
    cat > "$SEED_DIR/network-config" <<'EOF'
version: 2
ethernets:
  qemu:
    match:
      name: "e*"
    dhcp4: true
EOF
    hdiutil makehybrid -quiet -o "$SEED_ISO" -hfs -joliet -iso -default-volume-name cidata "$SEED_DIR"
    printf 'Preparing Debian kernel, build tools, and guest agent (first run can take several minutes)' >&2
    "$QEMU" \
        -machine q35 -accel tcg -cpu max -smp "$QEMU_CPUS" -m "$QEMU_MEMORY" \
        -drive "file=$PROVISION_DISK,format=qcow2,if=virtio" \
        -drive "file=$SEED_ISO,format=raw,media=cdrom,readonly=on" \
        -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
        -device virtio-serial -chardev null,id=qga0 \
        -device virtserialport,chardev=qga0,name=org.qemu.guest_agent.0 \
        -display none -serial "file:$SERIAL_LOG" -monitor none -no-reboot &
    QEMU_PID=$!
    finished=0
    for ((attempt = 0; attempt < QEMU_TIMEOUT; attempt++)); do
        if ! kill -0 "$QEMU_PID" 2>/dev/null; then
            if wait "$QEMU_PID"; then
                finished=1
            fi
            QEMU_PID=
            break
        fi
        if ((attempt % 10 == 0)); then printf '.' >&2; fi
        sleep 1
    done
    printf '\n' >&2
    if [[ "$finished" != 1 ]] || ! grep -q 'CC_VMTEST_READY' "$SERIAL_LOG"; then
        printf 'Guest preparation failed or timed out. Last serial log lines:\n' >&2
        tail -n 80 "$SERIAL_LOG" >&2
        exit 1
    fi
    mv "$PROVISION_DISK" "$GUEST_DISK"
    printf '%s\n' "$CACHE_KEY" > "$READY_FILE"
fi

# Quote paths as TOML strings so spaces, quotes, and backslashes survive.
toml_string() {
    local value=$1
    value=${value//\\/\\\\}
    value=${value//\"/\\\"}
    printf '"%s"' "$value"
}
CONFIG="$WORK_DIR/vmtest.toml"
{
    printf '[[target]]\nname = "C compiler tests (Debian x86_64)"\narch = "x86_64"\nimage = '
    toml_string "$GUEST_DISK"
    printf '\nqemu_command = '
    toml_string "$QEMU"
    cat <<'EOF'

command = '''
set -euo pipefail
uname -smr
work=$(mktemp -d /tmp/cc-vmtest.XXXXXX)
trap 'rm -rf -- "$work"' EXIT
tar -C /mnt/cc --exclude='./.git' --exclude='./.qemu' --exclude='*.qcow2' --exclude='*.download' --exclude='./cc' --exclude='./*.o' -cf - . | tar -C "$work" -xf -
make -C "$work" clean
make -C "$work" test
'''
[target.vm]
EOF
    printf 'num_cpus = %s\nmemory = ' "$QEMU_CPUS"
    toml_string "$QEMU_MEMORY"
    # vmtest disables VGA with -nodefaults. Debian's GRUB needs it to boot,
    # even with -display none; otherwise the guest never starts the agent.
    printf '\nextra_args = ["-vga", "std", "-cpu", "max"]\n[target.vm.mounts]\n"/mnt/cc" = { host_path = '
    toml_string "$SCRIPT_DIR"
    printf ', writable = false }\n'
} > "$CONFIG"

printf 'Running compiler tests with vmtest...\n'
"$VMTEST" --config "$CONFIG"
