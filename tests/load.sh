#!/bin/bash
# Screen load for performance tests: endless full-width scrolling text
while :; do
  head -c 4096 /dev/urandom | base64
done
