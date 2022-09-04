package com.project.sban;

public class Measure {
    public float humidity;
    public float temperature;
    public long timestamp;

    @Override
    public String toString() {
        return "Measure [humidity=" + humidity + ", temperature=" + temperature + ", timestamp=" + timestamp + "]";
    }
}
