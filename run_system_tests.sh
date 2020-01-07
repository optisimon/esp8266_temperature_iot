#!/bin/bash
#
# NOTE: You can only run system tests against a live unit.
#       Change the TARGET_IP to reach the temperature plotter.
#

TARGET_IP=192.168.0.1 python3 -m unittest discover -s system_tests -p 'test_*.py' -v
