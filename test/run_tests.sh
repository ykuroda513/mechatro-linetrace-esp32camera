#!/bin/sh
# ライン検出・制御ロジックのテストをPC上でビルドして実行する
set -e
cd "$(dirname "$0")"
SRC=../firmware/linetrace
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -I"$SRC" -o test_runner \
  test_main.cpp "$SRC/line_detector.cpp" "$SRC/controller.cpp" "$SRC/speed_ctrl.cpp"
./test_runner
