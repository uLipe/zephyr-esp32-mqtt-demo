#include <logging/log.h>
LOG_MODULE_REGISTER(zephyr_esp32_mqtt_demo, LOG_LEVEL_INF);

#include <zephyr.h>
#include <net/socket.h>
#include <net/mqtt.h>
#include <random/rand32.h>
#include <string.h>
#include <errno.h>

#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_event.h>

#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>

static uint8_t rx_buffer[256];
static uint8_t tx_buffer[256];
static struct mqtt_client client_ctx;
struct mqtt_utf8 app_user_name;
struct mqtt_utf8 app_password;

static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static struct net_mgmt_event_callback dhcp_cb;
static int nfds;

K_SEM_DEFINE(netif_ready, 0, 1);

#define MQTT_POLL_MSEC	500
#define MQTT_ESP32_DEMO_TOPIC "/esp32/hello"
#define MQTT_ESP32_DEMO_CLID "3.81.179.172"
#define MQTT_ESP32_DEMO_USER "ongkdrvv"
#define MQTT_ESP32_DEMO_PWD "HBYF91mehJcP"

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds[0].fd = client->transport.tcp.sock;
	}

	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static int poll_socks(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = zsock_poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void app_mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}
		LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}
		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		break;
	}
}

static int app_mqtt_publish(struct mqtt_client *client, enum mqtt_qos qos)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (uint8_t *)MQTT_ESP32_DEMO_TOPIC;
	param.message.topic.topic.size =
			strlen(param.message.topic.topic.utf8);
	param.message.payload.data = CONFIG_BOARD;
	param.message.payload.len =
			strlen(param.message.payload.data);
	param.message_id = sys_rand32_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	LOG_INF("Publish topic: %s", MQTT_ESP32_DEMO_TOPIC);
	LOG_INF("Publish data: %s", CONFIG_BOARD);

	return mqtt_publish(client, &param);
}

static void app_mqtt_broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(16328);

	zsock_inet_pton(AF_INET, MQTT_ESP32_DEMO_CLID, &broker4->sin_addr);
}

static void app_mqtt_client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	app_mqtt_broker_init();

	app_user_name.utf8 = (const uint8_t *)MQTT_ESP32_DEMO_USER;
	app_user_name.size = strlen(app_user_name.utf8);

	app_password.utf8 = (const uint8_t *) MQTT_ESP32_DEMO_PWD;
	app_password.size = strlen(app_password.utf8);

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = app_mqtt_evt_handler;
	client->client_id.utf8 = (const uint8_t *)"ESP32-Demo-Board";
	client->client_id.size = strlen(client->client_id.utf8);

	client->password = &app_password;
	client->user_name = &app_user_name;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
}

static int app_mqtt_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	app_mqtt_client_init(client);

	rc = mqtt_connect(client);
	if (rc != 0) {
		return rc;
	}

	prepare_fds(client);

	if (poll_socks(MQTT_POLL_MSEC)) {
		mqtt_input(client);
	}

	return rc;
}

static int app_mqtt_process_mqtt(struct mqtt_client *client)
{
	int rc;

	if (poll_socks(MQTT_POLL_MSEC)) {
		rc = mqtt_input(client);
		if (rc != 0) {
			return rc;
		}
	}

	rc = mqtt_live(client);
	if (rc != 0 && rc != -EAGAIN) {
		return rc;
	} else if (rc == 0) {
		rc = mqtt_input(client);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static void handler_cb(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}
	k_sem_give(&netif_ready);
}

static void wifi_interface_init(void)
{
	struct net_if *iface;

	net_mgmt_init_event_callback(&dhcp_cb, handler_cb,
				     NET_EVENT_IPV4_DHCP_BOUND);

	net_mgmt_add_event_callback(&dhcp_cb);

	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("wifi interface not available");
		return;
	}

	net_dhcpv4_start(iface);
	k_sem_take(&netif_ready, K_FOREVER);
}


void main(void)
{
	uint32_t count = 10;

	wifi_interface_init();
	app_mqtt_client_init(&client_ctx);

	app_mqtt_connect(&client_ctx);

	while(1) {		
		app_mqtt_process_mqtt(&client_ctx);
		if(count-- == 0) {
			count = 0;
			app_mqtt_publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
		}

	}
}
