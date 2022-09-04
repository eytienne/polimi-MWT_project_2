# polimi-MWT_project_2
Middleware technologies (2021-2022) Project 3: Smart Buildings and Neighborhoods


## Design notes

### Control loop
Sensors publish measures via the building to their rooms actuators. Those measures are bridged as output from the building broker to the backend broker.
The actuators will use the measures to set their associated HVAC properties. They will publish this setting back to the building broker for measurement simulation.

### Faked measures

The measures will be faked on a sensor level taking as input 1) the weather of the location (OpenWeather), 2) the previous measures and 3) the HVAC settings.

1\) and 3) are published periodically by the building and actuator respectively. 2) is kept by the sensor (initially set to a fake average one).

Further parameters would be needed to fake measures differently with room and sensor like bulding materials informations, room orientation, sensor position in a room, etc.

## Demonstration

### Install
```
apk add mosquitto-clients
mosquitto_sub -h building -t '#'
mosquitto_pub -h building -t 'test/alpha' -m 'Hello world!'

cd contiki-ng
# Pick contiki-ng from this branch of my fork to get working C++ linking: https://github.com/eytienne/contiki-ng/tree/eytienne
# Used by docker-compose
export $CNG_PATH=/path/to/fork
# Mounting the forked contiki-ng workspace to local versioned files
sudo mount --bind sensor/ /path/to/fork/examples/room-sensor/
sudo mount --bind building.csc /path/to/fork/simulations/building.csc
cd sensor
make fetch-include

docker build backend/ -f backend/Dockerfile.spark -t sban_spark
docker build actuator/ -t sban_actuator
docker build building/ -t sban_building
docker build contiki-ng/ -t sban_contiki-ng

cd backend
docker-compose pull
```

### Run
```
docker network create sban_backend-net

cd backend
# Following uses instructions from https://docs.confluent.io/platform/current/platform-quickstart.html#step-1-download-and-start-cp
# Bring up the kafka related services
docker-compose up -d
# Visit http://localhost:9021/
# Create topic `sban-measure`
# Import connector with config file `backend/MqttSourceConnector.properties`


# To select a docker-compose stack
source ./load-env.sh backend.env
docker-compose up -d

source ./load-env.sh stack1.env
# Deploy and operate on your stack with `docker-compose` without parameter needed
docker-compose up -d

# Run the simulation

# Reach the room actuator Node-RED and set up ROOM_ID in the flow environment panel then deploy it

docker-compose exec cooja bash
cd tools/cooja
ant run
# in another terminal
docker-compose exec cooja bash
cd examples/rpl-border-router
make TARGET=cooja connect-router-cooja

# Start the simulation
# Sensors will wait for configuration via MQTT

# Visit the building Node-RED instance dashboard: http://0.0.0.0:21880
# Attach the sensor(s) to rooms
# Turn them on

# Sensing begins and the room actuator console shows the actions taken on the HVAC setting!

```