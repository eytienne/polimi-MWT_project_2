FROM contiker/contiki-ng

RUN sudo apt install -y --no-install-recommends gettext-base && \
    sudo mkdir /run/mosquitto && sudo chown mosquitto /run/mosquitto

COPY ./mosquitto.template.conf /
COPY ./docker-entrypoint.sh /

ENTRYPOINT "/docker-entrypoint.sh"
