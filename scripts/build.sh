#!/bin/bash

set -e
ROOT_DIR="$(dirname "$(dirname "$(readlink -f "$0")")")"

check_build_file() {
  BUILD_DIRECTORY="$1"
  if [ -d "$BUILD_DIRECTORY" ]; then
    rm -rf "$BUILD_DIRECTORY"
  fi

  mkdir "$BUILD_DIRECTORY"
}

project_build() {
  BUILD_DIRECTORY="$1"
  cd "$BUILD_DIRECTORY"
  cmake ../
  make
}

BUILD_DIR="$ROOT_DIR/build"
check_build_file "$BUILD_DIR"
project_build "$BUILD_DIR"
