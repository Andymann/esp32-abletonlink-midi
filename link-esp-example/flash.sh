#!/bin/bash
# Pre-activate the Python 3.9 venv so idf_tools.py finds the correct env (idf4.4_py3.9_env)
# rather than looking for a non-existent idf4.4_py3.14_env (system python3 is 3.14)
export PATH="$HOME/.espressif/python_env/idf4.4_py3.9_env/bin:$PATH"
export IDF_PATH="$HOME/esp/v4.4/esp-idf"
source "$IDF_PATH/export.sh"
idf.py -p /dev/tty.usbserial-120  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 flash
