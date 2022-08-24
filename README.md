# polimi-MWT_project_2
Middleware technologies (2021-2022) Project 3: Smart Buildings and Neighborhoods

## Notes

```
docker build actuator/ -t sban_actuator
docker build building/ -t sban_building
docker build contiki-ng/ -t sban_contiki-ng

docker stack deploy -c <(docker-compose --env-file <ENV_FILE> --compose-file docker-compose.swarm.yaml config) <STACK>

sudo mount --bind contiki-ng/sensor/ ~/workspace/contiki-ng/examples/room-sensor/
sudo mount --bind contiki-ng/building.csc ~/workspace/contiki-ng/simulations/building.csc

make TARGET=cooja connect-router-cooja
```