#!/bin/bash

echo "Starting ESP32 monitor..."
echo "To exit: Press Ctrl+] (Control plus right square bracket)"
echo "---------------------------------------------"

# idf.py monitor
export PATH="$HOME/.espressif/python_env/idf4.4_py3.9_env/bin:$PATH"
export IDF_PATH="$HOME/esp/v4.4/esp-idf"
source "$IDF_PATH/export.sh"
idf.py -p /dev/tty.usbserial-110  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 monitor