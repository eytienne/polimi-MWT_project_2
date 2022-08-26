#!/bin/bash

envsubst < mosquitto.template.conf > /etc/mosquitto/mosquitto.conf
mosquitto -d -v -c /etc/mosquitto/mosquitto.conf

# nodered/node-red entrypoint
./entrypoint.sh