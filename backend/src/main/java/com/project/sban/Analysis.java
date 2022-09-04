package com.project.sban;

import static org.apache.spark.sql.functions.*;

import java.util.List;
import java.util.concurrent.TimeoutException;
import java.util.regex.Pattern;

import org.apache.hadoop.thirdparty.com.google.common.collect.Lists;
import org.apache.spark.sql.Column;
import org.apache.spark.sql.Dataset;
import org.apache.spark.sql.RelationalGroupedDataset;
import org.apache.spark.sql.Row;
import org.apache.spark.sql.RowFactory;
import org.apache.spark.sql.SparkSession;
import org.apache.spark.sql.api.java.UDF1;
import org.apache.spark.sql.streaming.StreamingQueryException;
import org.apache.spark.sql.types.DataTypes;
import org.apache.spark.sql.types.StructType;

import com.google.gson.Gson;
import com.google.gson.JsonSyntaxException;

import scala.collection.JavaConverters;
import scala.collection.Seq;

public class Analysis {

    static StructType localization = DataTypes.createStructType(List.of(
            DataTypes.createStructField("area", DataTypes.StringType, false),
            DataTypes.createStructField("building", DataTypes.StringType, false),
            DataTypes.createStructField("room", DataTypes.StringType, false)));
    static StructType measure = DataTypes.createStructType(List.of(
            DataTypes.createStructField("temperature", DataTypes.FloatType, false),
            DataTypes.createStructField("humidity", DataTypes.FloatType, true),
            DataTypes.createStructField("timestamp", DataTypes.LongType, true)));

    static Pattern topicPattern = Pattern.compile("area/(.*)/building/(.*)/room/(.*)/sensor/.*/measure");

    private static Row keyToLocalization(String key) throws JsonSyntaxException {
        var matcher = topicPattern.matcher(key);
        matcher.matches();
        return RowFactory.create(
                matcher.group(1),
                matcher.group(2),
                matcher.group(3));
    }

    static Gson gson = new Gson();

    // temporary replacement for from_json not working as expected giving null
    // values
    private static Row valueToLocalization(String value) throws JsonSyntaxException {
        var measure = gson.fromJson(value, Measure.class);
        return RowFactory.create(
                measure.temperature,
                measure.humidity,
                measure.timestamp);
    }

    static <T> Seq<T> toSeq(List<T> list) {
        return JavaConverters.asScalaIteratorConverter(list.iterator()).asScala().toSeq();
    }

    public static void main(String[] args) throws TimeoutException {
        final String master = args.length > 0 ? args[0] : "local[4]";

        final var spark = SparkSession.builder()
                .master(master)
                .appName("SbanAnalysis")
                .getOrCreate();

        spark.udf().register("keyToLocalization", (UDF1<String, Row>) Analysis::keyToLocalization, localization);
        spark.udf().register("valueToLocalization", (UDF1<String, Row>) Analysis::valueToLocalization, measure);
        Dataset<Row> df = spark
                .readStream()
                .format("kafka")
                .option("kafka.bootstrap.servers", "localhost:9092")
                .option("subscribe", "sban-measure")
                .load()
                .selectExpr("CAST(key AS STRING)", "CAST(value AS STRING)")
                .withColumn("keyData", callUDF("keyToLocalization", col("key")))
                .withColumn("jsonData", from_json(col("value"), measure))
                // .withColumn("jsonData", callUDF("valueToLocalization", col("value")))
                .select("keyData.*", "value", "jsonData.*")
                .selectExpr("*", "CAST(timestamp as timestamp) as _timestamp")
                .drop("timestamp")
                .selectExpr("*", "_timestamp as timestamp")
                .drop("_timestamp")
                .withWatermark("timestamp", "1 hour");

        df.printSchema();
        var perRoom = List.of(col("area"), col("building"), col("room")).toArray(Column[]::new);
        var perBuilding = List.of(col("area"), col("building")).toArray(Column[]::new);
        var perArea = List.of(col("area")).toArray(Column[]::new);

        var hourly = window(col("timestamp"), "10 seconds", "2 seconds");
        var daily = window(col("timestamp"), "60 seconds", "10 seconds");
        var weekly = window(col("timestamp"), "600 seconds", "60 seconds");

        var hourlyPerRoom = avgMeasure(df.groupBy(toSeq(Lists.asList(hourly, perRoom))));
        hourlyPerRoom.printSchema();
        var dailyPerRoom = avgMeasure(df.groupBy(toSeq(Lists.asList(daily, perRoom))));
        var weeklyPerRoom = avgMeasure(df.groupBy(toSeq(Lists.asList(weekly, perRoom))));

        var hourlyPerBuilding = avgMeasure(df.groupBy(toSeq(Lists.asList(hourly, perBuilding))));
        hourlyPerRoom.printSchema();
        var dailyPerBuilding = avgMeasure(df.groupBy(toSeq(Lists.asList(daily, perBuilding))));
        var weeklyPerBuilding = avgMeasure(df.groupBy(toSeq(Lists.asList(weekly, perBuilding))));

        // printQuery(df);

        printQuery(hourlyPerRoom);
        printQuery(dailyPerRoom);
        printQuery(weeklyPerRoom);

        printQuery(hourlyPerBuilding);
        printQuery(dailyPerBuilding);
        printQuery(weeklyPerBuilding);

        try {
            spark.streams().awaitAnyTermination();
        } catch (StreamingQueryException e) {
            e.printStackTrace();
        }
        spark.close();
    }

    static RelationalGroupedDataset dailyFrom(Dataset<Row> hourly) {
        return hourly.groupBy(
            window(col("timestamp"), "60 seconds", "10 seconds")
        );
    }

    static RelationalGroupedDataset weeklyFrom(Dataset<Row> daily) {
        return daily.groupBy(
                window(col("timestamp"), "600 seconds", "10 seconds"));
    }

    static Dataset<Row> avgMeasure(RelationalGroupedDataset rgd) {
        return rgd.agg(
            avg("humidity").alias("humidity"),
            avg("temperature").alias("temperature")
        );
    }

    static void printQuery(Dataset<Row> query) throws TimeoutException {
        query.writeStream()
                .outputMode("append")
                .format("console")
                .option("truncate", false)
                .start();
    }
}
