#!/bin/bash

envsubst < /mosquitto.template.conf > /etc/mosquitto/conf.d/mosquitto.conf

sudo mosquitto -v -c /etc/mosquitto/mosquitto.conf
