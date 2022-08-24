FROM contiker/contiki-ng

RUN sudo mkdir /run/mosquitto && sudo chown mosquitto /run/mosquitto

COPY ./mosquitto.conf /etc/mosquitto/conf.d/mosquitto.conf
COPY ./docker-entrypoint.sh /

ENTRYPOINT "/docker-entrypoint.sh"
