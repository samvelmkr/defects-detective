#!/bin/bash

set -e

if [[ "$EUID" -ne 0 ]]
then
  echo "Please run with root permission!" && exit 1
fi

ALL="all"
if [[ "$1" -eq "$ALL" ]]
then
  TIME_ZONE="Asia/Yerevan"
  ln -snf /usr/share/zoneinfo/"$TIME_ZONE" /etc/localtime && echo "$TIME_ZONE" > /etc/timezone
fi

apt-get update
apt-get install -y python3 python3-pip cmake clang-12 llvm-12 llvm-12-dev
