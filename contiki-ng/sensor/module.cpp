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

static struct mqtt_connection sub_conn;
static struct mqtt_connection pub_conn;

#define CLIENT_ID_SIZE 70
struct mqtt_conf_s {
	char broker_ip[64];
	uint16_t broker_port;
	char username[32];
	char password[32];
	char sub_client_id[CLIENT_ID_SIZE + 1];
	char pub_client_id[CLIENT_ID_SIZE + 1];
} mqtt_conf;

enum machine_state {
	STATE_INIT,
	STATE_REGISTERED,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_OFF,
	STATE_ON,
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
	measure(float humididty, float temperature): humididty(humididty), temperature(temperature) {}
	// initiliaze to some fake average value
	measure(): measure(50, 22) {}
};

static struct {
	int32_t cooling;
	int32_t heating;
	int32_t ventilation;
} hvacSetting = {0, 0, 0};

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

	int len = snprintf(mqtt_conf.sub_client_id, CLIENT_ID_SIZE,
			 "d:room-sensor:%02x%02x%02x%02x%02x%02x", linkaddr_node_addr.u8[0],
			 linkaddr_node_addr.u8[1], linkaddr_node_addr.u8[2],
			 linkaddr_node_addr.u8[5], linkaddr_node_addr.u8[6],
			 linkaddr_node_addr.u8[7]);
	strncpy(mqtt_conf.pub_client_id, mqtt_conf.sub_client_id, CLIENT_ID_SIZE);
	strncpy(mqtt_conf.sub_client_id + len, "-sub", CLIENT_ID_SIZE - len);
	strncpy(mqtt_conf.pub_client_id + len, "-pub", CLIENT_ID_SIZE - len);

	stringstream ss;
	ss << "room/+/sensor/" << sensorId << "/attach";
	attachTopic = ss.str();

	state = STATE_INIT;
}

static bool have_connectivity(void) {
	return uip_ds6_get_global(ADDR_PREFERRED) != NULL &&
		   uip_ds6_defrt_choose() != NULL;
}

#define mqtt_available(conn)                                                   \
	((!(conn)->out_queue_full) && (conn)->out_buffer_sent)

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
bool busy = false;
// avoid dangling pointers passing `c_str` to the mqtt process
static string currentTopic;
static string currentPayloadIfAny;

static void _subscribe(const string &topic) {
	auto _topic = topic.c_str();
	mqtt_status_t status =
		mqtt_subscribe(&sub_conn, NULL, (char *)_topic, MQTT_QOS_LEVEL_0);
	auto format = "Subscribing to '%s' with status %d\n";
	if (status == MQTT_STATUS_OK) {
		LOG_INFO(format, _topic, status);
	} else {
		LOG_ERR(format, _topic, status);
	}
}

static void _unsubscribe(const string &topic) {
	auto _topic = topic.c_str();
	mqtt_status_t status = mqtt_unsubscribe(&sub_conn, NULL, (char *)_topic);
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
		mqtt_publish(&pub_conn, NULL, (char *)_topic, (uint8_t *)payload.c_str(),
					 payload.size(), MQTT_QOS_LEVEL_1, MQTT_RETAIN_OFF);
	auto format = "Publishing to '%s' with status %d\n";
	if (status == MQTT_STATUS_OK) {
		LOG_INFO(format, _topic, status);
	} else {
		LOG_ERR(format, _topic, status);
	}
}

static void subscribe(const string &topic) {
	if (mqtt_queue.empty()) {
		busy = true;
		currentTopic = topic;
		_subscribe(currentTopic);
	}
	mqtt_queue.push_back({MQTT_SUBSCRIBE, {topic, ""}});
}

static void unsubscribe(const string &topic) {
	if (mqtt_queue.empty()) {
		busy = true;
		currentTopic = topic;
		_unsubscribe(currentTopic);
	}
	mqtt_queue.push_back({MQTT_UNSUBSCRIBE, {topic, ""}});
}

static void publish(const string &topic, const string &payload) {
	auto empty = mqtt_queue.empty();
	if (empty) {
		busy = true;
		currentTopic = topic;
		currentPayloadIfAny = payload;
		_publish(currentTopic, currentPayloadIfAny);
	}
	// keep payload if not just published
	mqtt_queue.push_back({MQTT_PUBLISH, {topic, empty ? "" : payload}});
	LOG_DBG("publish call %ld\n", mqtt_queue.size());
}

#define _ASSERT_FORMAT(expr)                                                   \
	if (!(expr)) {                                                             \
		LOG_ERR("Data format not recognised on topic %s\n", topic.c_str());    \
		break;                                                                 \
	}

static void sub_event_callback(struct mqtt_connection *m, mqtt_event_t event,
						   void *data) {
	switch (event) {
	case MQTT_EVENT_CONNECTED: {
		LOG_INFO("MQTT connected to %s:%d! (sub)\n", mqtt_conf.broker_ip,
				 mqtt_conf.broker_port);
		process_poll(&room_sensor_process);
		return;
	}
	case MQTT_EVENT_DISCONNECTED: {
		LOG_INFO("MQTT disconnected: reason %u (sub)\n", *((mqtt_event_t *)data));
		state = STATE_DISCONNECTED;
		process_poll(&room_sensor_process);
		return;
	}
	case MQTT_EVENT_PUBLISH: {
		struct mqtt_message *msg_ptr = (struct mqtt_message *)data;

		string topic(msg_ptr->topic);
		string payload((char *)msg_ptr->payload_chunk,
					   msg_ptr->payload_chunk_length);

		machine_state _state = state != STATE_MQTT_DEQUEUING ? state : previousState;
		switch (_state) {
		case STATE_OFF: {
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
				state = STATE_ON;
				LOG_INFO("Sensor turned on\n");
			} else {
				LOG_WARN("(connected) Topic unhandled: %s\n", topic.c_str());
			}
			break;
		}
		case STATE_ON: {
			if (topic == "sensors/off") {
				unsubscribe("sensors/off");
				unsubscribe("weather");
				unsubscribe("room/" + roomId + "/hvac");

				subscribe(attachTopic);
				subscribe("sensors/on");
				state = STATE_OFF;
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
				LOG_INFO("Received room HVAC setting %s\n", jsonPayload.dump().c_str());
			} else {
				LOG_WARN("(sensing) Topic unhandled: %s\n", topic.c_str());
			}
			break;
		}
		default:
			LOG_ERR("Should not receive messages at this point (topic = %s)\n",
					topic.c_str());
			break;
		}
		return;
	}
	case MQTT_EVENT_SUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Subscribed to '%s' successfully\n",
				 queued.second.topic.c_str());
		busy = false;
		return;
	}
	case MQTT_EVENT_UNSUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Unsubscribed from '%s' successfully\n",
				 queued.second.topic.c_str());
		busy = false;
		return;
	}
	default:
		LOG_WARN("Application got a unhandled MQTT event: %i (sub)\n", event);
		return;
	}
}

static void pub_event_callback(struct mqtt_connection *m, mqtt_event_t event,
						   void *data) {
	switch (event) {
	case MQTT_EVENT_CONNECTED: {
		LOG_INFO("MQTT connected to %s:%d! (pub)\n", mqtt_conf.broker_ip,
				 mqtt_conf.broker_port);
		process_poll(&room_sensor_process);
		return;
	}
	case MQTT_EVENT_DISCONNECTED: {
		LOG_INFO("MQTT disconnected: reason %u (pub)\n", *((mqtt_event_t *)data));
		state = STATE_DISCONNECTED;
		process_poll(&room_sensor_process);
		return;
	}
	case MQTT_EVENT_PUBACK: {
		auto queued = mqtt_queue.front();
		mqtt_queue.pop_front();
		LOG_INFO("Published from '%s' successfully\n",
				 queued.second.topic.c_str());
		busy = false;
		return;
	}
	default:
		LOG_WARN("Application got a unhandled MQTT event: %i (pub)\n", event);
		return;
	}
}

extern "C" void state_machine() {
	LOG_DBG("state_machine %d %ld\n", state, mqtt_queue.size());

	if (state != STATE_MQTT_DEQUEUING && !mqtt_queue.empty()) {
		previousState = state;
		state = STATE_MQTT_DEQUEUING;
	} else if(state == STATE_MQTT_DEQUEUING && mqtt_queue.empty()) {
		LOG_DBG("queue empty %d\n", previousState);
		state = previousState;
	}

	switch (state) {
	case STATE_INIT: {
		mqtt_register(&sub_conn, &room_sensor_process, mqtt_conf.sub_client_id,
					  sub_event_callback, MAX_TCP_SEGMENT_SIZE);
		mqtt_register(&pub_conn, &room_sensor_process, mqtt_conf.pub_client_id,
					  pub_event_callback, MAX_TCP_SEGMENT_SIZE);

		state = STATE_REGISTERED;
		LOG_INFO("MQTT registered\n");
	}
	case STATE_REGISTERED:
		if (!have_connectivity()) {
			LOG_WARN("No connectivity!\n");
			etimer_set(&et, CLOCK_SECOND);
		}
		if(!mqtt_connected(&sub_conn)) {
			mqtt_status_t status = mqtt_connect(
				&sub_conn, mqtt_conf.broker_ip, mqtt_conf.broker_port,
				60 * CLOCK_SECOND, MQTT_CLEAN_SESSION_ON);
			LOG_INFO("MQTT connecting with status %d (sub)\n", status);
		}
		if(!mqtt_connected(&pub_conn)) {
			mqtt_status_t status = mqtt_connect(
				&pub_conn, mqtt_conf.broker_ip, mqtt_conf.broker_port,
				60 * CLOCK_SECOND, MQTT_CLEAN_SESSION_ON);
			LOG_INFO("MQTT connecting with status %d (pub)\n", status);
		}

		state = STATE_CONNECTING;
		/* continue to next case */
	case STATE_CONNECTING:
		if (mqtt_connected(&sub_conn) && mqtt_connected(&pub_conn)) {
			state = STATE_CONNECTED;
			process_poll(&room_sensor_process);
			return;
		}
		LOG_INFO("MQTT connecting...\n");
		etimer_set(&et, CLOCK_SECOND);
		return;
	case STATE_CONNECTED:
		if (!mqtt_available(&sub_conn)) {
			LOG_WARN("Connection not available for configuration\n");
			etimer_set(&et, CLOCK_SECOND);
			return;
		}
		subscribe(attachTopic);
		subscribe("sensors/on");
		state = STATE_OFF;
		/* continue to next case */
	case STATE_OFF:
		LOG_INFO("Waiting for configuration\n");
		etimer_set(&et, 2*CLOCK_SECOND);
		return;
	case STATE_ON:
		if (!mqtt_available(&pub_conn)) {
			LOG_WARN("Connection not available for publishing\n");
			etimer_set(&et, CLOCK_SECOND);
			return;
		} else if(etimer_expired(&et)) {
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
		if (busy) {
			LOG_WARN("MQTT busy\n");
		} else {
			auto queued = mqtt_queue.front();
			if (!mqtt_available(queued.first == MQTT_PUBLISH ? &pub_conn : &sub_conn)) {
				LOG_WARN("Connection not available for dequeuing\n");
			} else {
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
				process_poll(&room_sensor_process);
				return;
			}
		}
		etimer_set(&et, CLOCK_SECOND);
		return;
	case STATE_DISCONNECTED:
		state = STATE_REGISTERED;
		process_poll(&room_sensor_process);
		return;
	}
}
