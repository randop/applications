#!/bin/sh
# detect-distro.sh — POSIX shell script to identify Linux distribution family
# Detects: Debian-based, Ubuntu, Artix, Arch, RedHat-based, Fedora

# Prefer /etc/os-release (standard on modern systems)
if [ -r /etc/os-release ]; then
  # shellcheck disable=SC1091
  . /etc/os-release
else
  ID=""
  ID_LIKE=""
  NAME=""
fi

# Normalize to lowercase for easier matching
id=$(printf '%s' "$ID" | tr '[:upper:]' '[:lower:]')
id_like=$(printf '%s' "$ID_LIKE" | tr '[:upper:]' '[:lower:]')
name=$(printf '%s' "$NAME" | tr '[:upper:]' '[:lower:]')

# Helper: check if a string contains a word (POSIX-safe)
contains() {
  # $1 = haystack, $2 = needle
  case " $1 " in
  *" $2 "*) return 0 ;;
  *) return 1 ;;
  esac
}

# NOTE: order matters, more specific first

# 1. Ubuntu (special case of Debian)
if [ "$id" = "ubuntu" ] || contains "$id_like" "ubuntu"; then
  echo "ubuntu"
  exit 0
fi

# 2. Debian-based (includes pure Debian, and derivatives that are not Ubuntu)
if [ "$id" = "debian" ] ||
  contains "$id_like" "debian" ||
  [ -f /etc/debian_version ]; then
  echo "debian-based"
  exit 0
fi

# 3. Artix Linux
if [ "$id" = "artix" ] || contains "$name" "artix"; then
  echo "artix"
  exit 0
fi

# 4. Arch Linux
if [ "$id" = "arch" ] ||
  contains "$id_like" "arch" ||
  [ -f /etc/arch-release ]; then
  echo "arch"
  exit 0
fi

# 5. Fedora
if [ "$id" = "fedora" ] || contains "$id_like" "fedora"; then
  echo "fedora"
  exit 0
fi

# 6. RedHat-based (RHEL, CentOS, Rocky, Alma, Oracle, etc.)
if [ "$id" = "rhel" ] || [ "$id" = "centos" ] ||
  [ "$id" = "rocky" ] || [ "$id" = "almalinux" ] ||
  [ "$id" = "ol" ] || [ "$id" = "redhat" ] ||
  contains "$id_like" "rhel" || contains "$id_like" "fedora" ||
  [ -f /etc/redhat-release ]; then
  # Note: Fedora also has /etc/redhat-release on some versions,
  # but we already caught Fedora above.
  echo "redhat-based"
  exit 0
fi

# Fallback
echo "unknown"
exit 1
