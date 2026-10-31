#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# median-of-5 timings pinned to core 7
cd "$(dirname "$0")"
OUT=bench_final_raw.txt; : > $OUT
for B in b2_fast b2_libm; do for L in 8 16 32; do
  for cfg in "esav 0" "esav 4" "esav 15" "rk4 0" "gs 0"; do
    for r in 1 2 3 4 5; do echo "$B $(taskset -c 7 ./$B $L $cfg)" >> $OUT; done
  done
  if [ $L -le 16 ]; then for r in 1 2 3; do echo "$B $(taskset -c 7 ./$B $L lit_dense 0)" >> $OUT; done; fi
done; done
uptime >> $OUT
