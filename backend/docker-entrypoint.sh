#!/bin/bash
set -e

if [ "$SPARK_MODE" == "master" ];
then
    /opt/spark/sbin/start-master.sh --properties-file ../conf/spark-defaults.conf
elif [ "$SPARK_MODE" == "worker" ];
then
    /opt/spark/sbin/start-worker.sh $SPARK_MASTER_URL
else
    echo "Undefined $SPARK_MODE, must specify: master, worker"
    exit 1
fi

tail -f /dev/null
