extern "C" {
#include "contiki.h"
#define new __new
#include "mqtt.h"
#undef new
#include "leds.h"
// avoid linkage problem with json.hpp
#include "lib/assert.h"
#include "net/ipv6/sicslowpan.h"
#include "net/ipv6/uip.h"
#include "rpl.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"

#include "./room-sensor.h"
extern struct etimer et;
}
#include "./include/json.hpp"
#include <deque>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

using namespace std;
using json = nlohmann::json;

extern int sensorId;

PROCESS_NAME(room_sensor_process);

/* Maximum TCP segment size for outgoing segments of our socket */
#define MAX_TCP_SEGMENT_SIZE 32

#define DEFAULT_USERNAME "use-token-auth"
#define DEFAULT_PASSWORD "AUTHZ"
#define DEFAULT_BROKER_PORT 1883

static struct mqtt_connection conn;
static struct ctimer ct;

#define CLIENT_ID_SIZE 70
struct mqtt_conf_s {
	char broker_ip[64];
	uint16_t broker_port;
	char username[32];
	char password[32];
	char client_id[CLIENT_ID_SIZE + 1];
} mqtt_conf;

enum machine_state {
	STATE_INIT,
	STATE_REGISTERED,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_SENSING,
	STATE_MQTT_DEQUEUING,
	STATE_DISCONNECTED,
};

static machine_state state;
static machine_state previousState;

static string attachTopic;
static string roomId;

struct measure {
	float humididty;
	float temperature;
};

static struct {
	int32_t cooling;
	int32_t heating;
	int32_t ventilation;
} hvacSetting;

static measure weatherMeasure;
static measure previousMeasure;

static measure getFakeMeasure() {
	measure ret = {
		(previousMeasure.humididty + weatherMeasure.humididty) / 2 -
			hvacSetting.ventilation,
		(previousMeasure.temperature + weatherMeasure.temperature) / 2 -
			hvacSetting.cooling + hvacSetting.heating,
	};
	return ret;
}

extern "C" void initialize() {
	memcpy(mqtt_conf.broker_ip, PROJECT_CONF_BROKER_IP_ADDR,
		   strlen(PROJECT_CONF_BROKER_IP_ADDR));
	mqtt_conf.broker_port = DEFAULT_BROKER_PORT;
	memcpy(mqtt_conf.username, DEFAULT_USERNAME, strlen(DEFAULT_USERNAME));
	memcpy(mqtt_conf.password, DEFAULT_PASSWORD, strlen(DEFAULT_PASSWORD));

	snprintf(mqtt_conf.client_id, CLIENT_ID_SIZE,
			 "d:room-sensor:%02x%02x%02x%02x%02x%02x", linkaddr_node_addr.u8[0],
			 linkaddr_node_addr.u8[1], linkaddr_node_addr.u8[2],
			 linkaddr_node_addr.u8[5], linkaddr_node_addr.u8[6],
			 linkaddr_node_addr.u8[7]);
	mqtt_conf.client_id[CLIENT_ID_SIZE - 1] = '\0';

	stringstream ss;
	ss << "room/+/sensor/" << sensorId << "/attach";
	attachTopic = ss.str();

	// initiliaze to some fake average value
	previousMeasure = {50, 22};

	state = STATE_INIT;
}

static bool have_connectivity(void) {
	return uip_ds6_get_global(ADDR_PREFERRED) != NULL &&
		   uip_ds6_defrt_choose() != NULL;
}

#define mqtt_available(conn)                                                   \
	((!(conn)->out_queue_full) && (conn)->out_buffer_sent)

static void publish_led_on() { leds_on(PROJECT_CONF_STATUS_LED); }
static void publish_led_off(void *d) { leds_off(PROJECT_CONF_STATUS_LED); }

enum mqtt_operation {
	MQTT_SUBSCRIBE,
	MQTT_UNSUBSCRIBE,
	MQTT_PUBLISH,
};

struct mqtt_operands {
	string topic;
	string payloadIfAny;
};

// using a queue missing from contiki-ng
static deque<pair<mqtt_operation, mqtt_operands>> mqtt_queue;
// avoid dangling pointers passing `c_str` to the mqtt process
static string currentTopic;
static string currentPayloadIfAny;

static void _subscribe(const string &topic) {
	auto _topic = topic.c_str();
	mqtt_status_t status =
		mqtt_subscribe(&conn, NULL, (char *)_topic, MQTT_QOS_LEVEL_0);
	auto format = "Subscribing to '%s' with status %d\n";
	if (status == MQTT_STATUS_OK) {
		LOG_INFO(format, _topic, status);
	} else {
		LOG_ERR(format, _topic, status);
	}
}

static void _unsubscribe(const string &topic) {
	auto _topic = topic.c_str();
	mqtt_status_t status = mqtt_unsubscribe(&conn, NULL, (char *)_topic);
	auto format = "Unsubscribing from '%s' with status %d\n";
	if (status == MQTT_STATUS_OK) {
		LOG_INFO(format, _topic, status);
	} else {
		LOG_ERR(format, _topic, status);
	}
}

static void _publish(const string &topic, const string &payload) {
	auto _topic = topic.c_str();
	mqtt_status_t status =
		mqtt_publish(&conn, NULL, (char *)_topic, (uint8_t *)_topic,
					 payload.size(), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
	auto format = "Publishing to '%s' with status %d\n";
	if (status == MQTT_STATUS_OK) {
		LOG_INFO(format, _topic, status);
	} else {
		LOG_ERR(format, _topic, status);
	}
}

static void dequeuing() {
	if (state != STATE_MQTT_DEQUEUING) {
		previousState = state;
		state = STATE_MQTT_DEQUEUING;
	}
	process_poll(&room_sensor_process);
}

static void subscribe(const string &topic) {
	if (mqtt_queue.empty()) {
		currentTopic = topic;
		_subscribe(currentTopic);
	}
	mqtt_queue.push_back({MQTT_SUBSCRIBE, {topic, ""}});
}

static void unsubscribe(const string &topic) {
	if (mqtt_queue.empty()) {
		currentTopic = topic;
		_unsubscribe(currentTopic);
	}
	mqtt_queue.push_back({MQTT_UNSUBSCRIBE, {topic, ""}});
}

static void publish(const string &topic, const string &payload) {
	auto empty = mqtt_queue.empty();
	if (empty) {
		currentTopic = topic;
		currentPayloadIfAny = payload;
		_publish(currentTopic, currentPayloadIfAny);
	}
	// keep payload if not just published
	mqtt_queue.push_back({MQTT_PUBLISH, {topic, empty ? "" : payload}});
}

#define _ASSERT_FORMAT(expr)                                                   \
	if (!(expr)) {                                                             \
		LOG_ERR("Data format not recognised on topic %s", topic.c_str());      \
	}

static void event_callback(struct mqtt_connection *m, mqtt_event_t event,
						   void *data) {
	switch (event) {
	case MQTT_EVENT_CONNECTED: {
		LOG_INFO("MQTT connected to %s:%d!\n", mqtt_conf.broker_ip,
				 mqtt_conf.broker_port);
		subscribe(attachTopic);
		subscribe("sensors/on");
		state = STATE_CONNECTED;
		process_poll(&room_sensor_process);
		break;
	}
	case MQTT_EVENT_DISCONNECTED: {
		LOG_INFO("MQTT disconnected: reason %u\n", *((mqtt_event_t *)data));
		state = STATE_DISCONNECTED;
		process_poll(&room_sensor_process);
		return;
	}
	case MQTT_EVENT_PUBLISH: {
		struct mqtt_message *msg_ptr = (struct mqtt_message *)data;

		string topic(msg_ptr->topic);
		string payload((char *)msg_ptr->payload_chunk,
					   msg_ptr->payload_chunk_length);

		switch (state) {
		case STATE_CONNECTED: {
			std::smatch roomMatch;
			if (std::regex_match(topic, roomMatch,
								 regex("room/(\\d+)/sensor/(\\d+)/attach"))) {
				roomId = roomMatch[1].str();
				LOG_INFO("Attached to room %s\n", roomId.c_str());
			} else if (topic == "sensors/on") {
				if (roomId.empty()) {
					LOG_ERR("Cannot be turned on, must be attached to room "
							"first\n");
					break;
				}
				unsubscribe(attachTopic);
				unsubscribe("sensors/on");

				subscribe("sensors/off");
				subscribe("weather");
				subscribe("room/" + roomId + "/hvac");
				state = STATE_SENSING;
				LOG_INFO("Sensor turned on\n");
			} else {
				LOG_WARN("(connected) Topic unhandled: %s\n", topic.c_str());
			}
			break;
		}
		case STATE_SENSING: {
			if (topic == "sensors/off") {
				unsubscribe("sensors/off");
				unsubscribe("weather");
				unsubscribe("room/" + roomId + "/hvac");

				subscribe(attachTopic);
				subscribe("sensors/on");
				state = STATE_CONNECTED;
				LOG_INFO("Sensor turned off\n");
			} else if (topic == "weather") {
				auto jsonPayload = json::parse(payload);
				auto humidity = jsonPayload["humidity"];
				auto temperature = jsonPayload["tempc"];
				_ASSERT_FORMAT(humidity.is_number_float() &&
							   temperature.is_number_float());
				weatherMeasure = {humidity, temperature};
				LOG_INFO("Received weather %s\n", jsonPayload.dump().c_str());
			} else if (topic == "room/" + roomId + "/hvac") {
				auto jsonPayload = json::parse(payload);
				auto cooling = jsonPayload["cooling"];
				auto heating = jsonPayload["heating"];
				auto ventilation = jsonPayload["ventilation"];
				_ASSERT_FORMAT(cooling.is_number_integer() &&
							   heating.is_number_integer() &&
							   ventilation.is_number_integer());
				hvacSetting = {cooling, heating, ventilation};
			} else {
				LOG_WARN("(sensing) Topic unhandled: %s\n", topic.c_str());
			}
		} break;
		default:
			LOG_ERR("Should not receive messages at this point (topic = %s)\n",
					topic.c_str());
			break;
		}

		break;
	}
	case MQTT_EVENT_SUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Subscribed to '%s' successfully\n",
				 queued.second.topic.c_str());
		dequeuing();
		break;
	}
	case MQTT_EVENT_UNSUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Unsubscribed from '%s' successfully\n",
				 queued.second.topic.c_str());
		dequeuing();
		break;
	}
	case MQTT_EVENT_PUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Published from '%s' successfully\n",
				 queued.second.topic.c_str());
		dequeuing();
		break;
	}
	default:
		LOG_WARN("Application got a unhandled MQTT event: %i\n", event);
		break;
	}
}


extern "C" void state_machine() {
	switch (state) {
	case STATE_INIT:
		mqtt_register(&conn, &room_sensor_process, mqtt_conf.client_id,
					  event_callback, MAX_TCP_SEGMENT_SIZE);

		// mqtt_set_username_password(&conn, mqtt_conf.username,
		// 						   mqtt_conf.password);

		state = STATE_REGISTERED;
		LOG_INFO("MQTT registered\n");
	case STATE_REGISTERED:
		if (!have_connectivity()) {
			publish_led_on();
			ctimer_set(&ct, CLOCK_SECOND >> 3, publish_led_off, NULL);
			LOG_WARN("No connectivity!\n");
		} else {
			mqtt_status_t status = mqtt_connect(
				&conn, mqtt_conf.broker_ip, mqtt_conf.broker_port,
				60 * CLOCK_SECOND, MQTT_CLEAN_SESSION_ON);

			LOG_INFO("MQTT connecting with status %d\n", status);
			state = STATE_CONNECTING;
		}

		etimer_set(&et, CLOCK_SECOND);
		return;
	case STATE_CONNECTING:
		if (mqtt_connected(&conn)) {
			state = STATE_CONNECTED;
			etimer_set(&et, 1);
			return;
		}
		publish_led_on();
		ctimer_set(&ct, CLOCK_SECOND >> 2, publish_led_off, NULL);
		LOG_INFO("MQTT connecting...\n");
		etimer_set(&et, CLOCK_SECOND);
		return;
	case STATE_CONNECTED:
		LOG_INFO("Waiting for attachment\n");
		etimer_set(&et, 2*CLOCK_SECOND);
		return;
	case STATE_SENSING:
		if (!mqtt_connected(&conn)) {
			LOG_ERR("Not connected for publishing\n");
			break;
		} else if (!mqtt_available(&conn)) {
			LOG_ERR("Connection not available for publishing\n");
			break;
		} else {
			string measureTopic = "room/" + roomId + "/sensor/" +
								  to_string(sensorId) + "/measure";
			auto mm = getFakeMeasure();
			previousMeasure = mm;
			json jsonPayload = {
				{"humidity", mm.humididty},
				{"temperature", mm.temperature},
			};
			auto stringPayload = jsonPayload.dump();
			publish(measureTopic, stringPayload);
			etimer_set(&et, 5 * CLOCK_SECOND);
			return;
		}
	case STATE_MQTT_DEQUEUING:
		LOG_DBG("STATE_MQTT_DEQUEUING reached\n");
		if (mqtt_queue.empty()) {
			state = previousState;
			break;
		}
		if (!mqtt_connected(&conn)) {
			LOG_ERR("Not connected for dequeuing\n");
			break;
		} else if (!mqtt_available(&conn)) {
			LOG_ERR("Connection not available for dequeuing\n");
		} else {
			auto queued = mqtt_queue.front();
			currentTopic = queued.second.topic;
			currentPayloadIfAny = queued.second.payloadIfAny;
			switch (queued.first) {
			case MQTT_SUBSCRIBE:
				_subscribe(currentTopic);
				break;
			case MQTT_UNSUBSCRIBE:
				_unsubscribe(currentTopic);
				break;
			case MQTT_PUBLISH:
				_publish(currentTopic, currentPayloadIfAny);
				break;
			}
		}
		break;
	case STATE_DISCONNECTED:
		state = STATE_CONNECTING;
		break;
	}

	/* If we didn't return so far, reschedule ourselves */
	etimer_set(&et, CLOCK_SECOND >> 1);
}
