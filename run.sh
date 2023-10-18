#!/bin/bash

ROOT_DIR="$(dirname "$(readlink -f "$0")")"

function check_arguments() {
  FIRST_ARG="$1"
  if [ -z "$FIRST_ARG" ]; then
    echo "Give correct argument: "
    echo -e "\tAs an argument pass the bitcode file."
    exit 1
  fi

  if [ ! -f "$FIRST_ARG" ]; then
    echo "$FIRST_ARG should be an existing bitcode."
    exit 1
  fi
}

function check_build() {
  if [ ! -f "$PASS_PATH" ]; then
    echo "At first build the project."
    exit 1
  fi
}

function run_pass() {
  opt-12 -load-pass-plugin "$PASS_PATH" -passes=simple -disable-output "$GIVEN_BC"
  FILE=$(basename "$GIVEN_BC")
  FILE_NAME=${FILE%.*}
  if [ ! -f "report.sarif" ]; then
    echo "Report for '$FILE_NAME' is not generated!"
  fi
}

function main() {
  check_arguments "$1"

  GIVEN_BC=$(realpath "$1")
  PASS_PATH="$ROOT_DIR/build/src/libAnalyzer.so"

  check_build
  run_pass
}

main "$@"
