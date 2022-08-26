# polimi-MWT_project_2
Middleware technologies (2021-2022) Project 3: Smart Buildings and Neighborhoods


## Design notes

### Control loop
Sensors publish measures via the building to their rooms actuators. Those measures are bridged as output from the building broker to the backend broker.
The actuators will use the measures to set their associated HVAC properties. They will publish this setting back to the building broker for measurement simulation.

### Faked measures
The measures will be faked on a sensor level taking as input 1) the outside measures of the location (OpenWeather), 2) the previous measures, 3) the HVAC settings and 4) a sensor bias (could reflect the positioning in the room).
1) and 3) are published periodically by the building and actuator respectively. 2) is kept by the sensor (initially set to a fake average one) and 4) is configured once at start-up.

## Commands
```
docker build actuator/ -t sban_actuator
docker build building/ -t sban_building
docker build contiki-ng/ -t sban_contiki-ng

docker network create sban_backend-net

# Select a stack
source ./load-env.sh stack1.env
# Deploy and operate on your stack with `docker-compose` without parameter needed
docker-compose up -d

apk add mosquitto-clients
mosquitto_sub -h building -t '#'
mosquitto_pub -h building -t 'test/alpha' -m 'Hello world!'

sudo mount --bind contiki-ng/sensor/ ~/workspace/contiki-ng/examples/room-sensor/
sudo mount --bind contiki-ng/building.csc ~/workspace/contiki-ng/simulations/building.csc

make TARGET=cooja connect-router-cooja
```