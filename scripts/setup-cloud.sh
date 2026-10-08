#!/usr/bin/env bash
set -euo pipefail

# User-local dependencies for the isolated Digitakt C++/JUCE environment.
# Repository files and the system package database remain untouched.
task_tools_dir="${DIGITAKT_TOOLS_DIR:-/workspace/.digitakt-tools}"
mkdir -p "$task_tools_dir" "$task_tools_dir/cache/fontconfig" "$task_tools_dir/config"
if [[ ! -x "$task_tools_dir/python/bin/python" ]]; then
  python3 -m venv "$task_tools_dir/python"
fi
"$task_tools_dir/python/bin/python" -m pip install --disable-pip-version-check --no-cache-dir \
  cmake==3.31.6 ninja==1.11.1.4

mkdir -p "$task_tools_dir/apt/etc/apt.conf.d" "$task_tools_dir/apt/etc/preferences.d" "$task_tools_dir/apt/state/lists/partial" \
  "$task_tools_dir/apt/cache/archives/partial" "$task_tools_dir/apt/log" "$task_tools_dir/sysroot"
cp /var/lib/dpkg/status "$task_tools_dir/apt/state/status"
cat > "$task_tools_dir/apt/etc/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie-updates main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
cat > "$task_tools_dir/apt/apt.conf" <<CONFIG
Dir::Etc "$task_tools_dir/apt/etc";
Dir::Etc::sourcelist "sources.list";
Dir::Etc::sourceparts "-";
Dir::Etc::main "-";
Dir::Etc::parts "apt.conf.d";
Dir::State "$task_tools_dir/apt/state";
Dir::State::status "$task_tools_dir/apt/state/status";
Dir::Cache "$task_tools_dir/apt/cache";
Dir::Log "$task_tools_dir/apt/log";
APT::Sandbox::User "$(id -un)";
Acquire::Languages "none";
CONFIG

# APT validates signed Release metadata and each artifact checksum; HTTPS
# certificate verification is left enabled. Download-only does not install.
env APT_CONFIG="$task_tools_dir/apt/apt.conf" /usr/bin/apt-get update
env APT_CONFIG="$task_tools_dir/apt/apt.conf" /usr/bin/apt-get \
  --download-only --no-install-recommends --yes install \
  libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxrender-dev libfreetype-dev libfontconfig1-dev xvfb xauth

# Extract matching runtime libraries even when they are already installed in
# the base image, so development symlinks and pkg-config dependencies resolve.
(
  cd "$task_tools_dir/apt/cache/archives"
  env APT_CONFIG="$task_tools_dir/apt/apt.conf" /usr/bin/apt-get download \
    libfreetype6 libfontconfig1 libx11-6 libxext6 libxrandr2 libxinerama1 \
    libxcursor1 libxrender1 libxfixes3 libxau6 libxdmcp6 libxcb1 \
    zlib1g zlib1g-dev libbrotli1 libbrotli-dev libexpat1 libexpat1-dev \
    libbz2-1.0 libuuid1
)

for task_deb_file in "$task_tools_dir"/apt/cache/archives/*.deb; do
  [[ -f "$task_deb_file" ]] || continue
  /usr/bin/dpkg-deb --extract "$task_deb_file" "$task_tools_dir/sysroot"
done

find "$task_tools_dir/apt/cache/archives" -maxdepth 1 -name '*.deb' -print0 \
  | sort -z | xargs -0 -r sha256sum > "$task_tools_dir/debian-artifacts.sha256"
for task_deb_file in "$task_tools_dir"/apt/cache/archives/*.deb; do
  [[ -f "$task_deb_file" ]] || continue
  /usr/bin/dpkg-deb --show --showformat='${Package} ${Version} ${Architecture}\n' "$task_deb_file"
done | sort > "$task_tools_dir/debian-versions.txt"
"$task_tools_dir/python/bin/cmake" --version
"$task_tools_dir/python/bin/ninja" --version

cat > "$task_tools_dir/env.sh" <<'ENVIRONMENT'
#!/usr/bin/env bash
task_tools_dir="${DIGITAKT_TOOLS_DIR:-/workspace/.digitakt-tools}"
export DIGITAKT_TOOLS_DIR="$task_tools_dir"
export XDG_CACHE_HOME="$task_tools_dir/cache"
export XDG_CONFIG_HOME="$task_tools_dir/config"
export PATH="$task_tools_dir/python/bin:$task_tools_dir/sysroot/usr/bin:$PATH"
export PKG_CONFIG_SYSROOT_DIR="$task_tools_dir/sysroot"
export PKG_CONFIG_LIBDIR="$task_tools_dir/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$task_tools_dir/sysroot/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export CMAKE_PREFIX_PATH="$task_tools_dir/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export CMAKE_INCLUDE_PATH="$task_tools_dir/sysroot/usr/include${CMAKE_INCLUDE_PATH:+:$CMAKE_INCLUDE_PATH}"
export CMAKE_LIBRARY_PATH="$task_tools_dir/sysroot/usr/lib/x86_64-linux-gnu${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"
export LD_LIBRARY_PATH="$task_tools_dir/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset task_tools_dir
ENVIRONMENT
