FROM nodered/node-red:3.0.2

USER root

RUN apk add --no-cache gettext mosquitto

COPY package.json ./
RUN npm install --unsafe-perm --no-update-notifier --no-fund --only=production
COPY ./data/* /data/

COPY ./mosquitto.template.conf ./
COPY ./docker-entrypoint.sh ./
EXPOSE 1883

ENTRYPOINT [ "/usr/src/node-red/docker-entrypoint.sh" ]