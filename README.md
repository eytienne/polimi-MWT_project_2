# polimi-MWT_project_2
Middleware technologies (2021-2022) Project 3: Smart Buildings and Neighborhoods


## Design notes

### Control loop
Sensors publish measures via the building to their rooms actuators. Those measures are bridged as output from the building broker to the backend broker.
The actuators will use the measures to set their associated HVAC properties. They will publish this setting back to the building broker for measurement simulation.

### Faked measures

1) and 3) are published periodically by the building and actuator respectively. 2) is kept by the sensor (initially set to a fake average one).
The measures will be faked on a sensor level taking as input 1) the weather of the location (OpenWeather), 2) the previous measures and 3) the HVAC settings.

Further parameters would be needed to fake measures differently with room and sensor like bulding materials informations, room orientation, sensor position in a room, etc.

## Commands
```
docker network create sban_backend-net

docker build actuator/ -t sban_actuator
docker build building/ -t sban_building
docker build contiki-ng/ -t sban_contiki-ng

# Select a stack
source ./load-env.sh stack1.env
# Deploy and operate on your stack with `docker-compose` without parameter needed
docker-compose up -d

apk add mosquitto-clients
mosquitto_sub -h building -t '#'
mosquitto_pub -h building -t 'test/alpha' -m 'Hello world!'

cd contiki-ng

# Mounting distant contiki-ng workspace to local versioned files
sudo mount --bind sensor/ ~/workspace/contiki-ng/examples/room-sensor/
sudo mount --bind building.csc ~/workspace/contiki-ng/simulations/building.csc

make fetch-include

# (in contiki-ng/examples/rpl-border-router)
make TARGET=cooja connect-router-cooja


docker build backend/ -f backend/Dockerfile.spark -t sban_spark
source ./load-env.sh backend.env
docker-compose up -d

../sbin/start-history-server.sh
```

MQTT Connector
tcp://mqtt.sban.org
sban-measure
area/+/building/+/room/+/sensor/+/measure