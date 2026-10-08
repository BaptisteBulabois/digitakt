#!/usr/bin/env bash
set -euo pipefail
task_repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f /workspace/.digitakt-tools/env.sh ]]; then
  source /workspace/.digitakt-tools/env.sh
fi
task_args=()
if [[ -f "$task_repo_dir/.deps/JUCE/CMakeLists.txt" ]]; then
  task_args+=(-DTAKT_JUCE_PATH="$task_repo_dir/.deps/JUCE")
fi
cmake -S "$task_repo_dir" -B "$task_repo_dir/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release "${task_args[@]}" "$@"
cmake --build "$task_repo_dir/build" --parallel "${TAKT_BUILD_JOBS:-3}"
ctest --test-dir "$task_repo_dir/build" --output-on-failure
