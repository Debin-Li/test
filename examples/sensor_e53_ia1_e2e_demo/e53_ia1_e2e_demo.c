#include "e53_ia1_e2e_demo.h"
#include "e53_ia1_e2e_demo_config.h"
#include "cJSON.h"
#include "E53_IA1.h"
#include "mcu_init.h"

#define  USE_ESP8266
//#define MQTT_SERVER_IP          "iotcloud-mqtt.gz.tencentdevices.com"
#define MQTT_SERVER_IP          "111.230.127.136"
#define MQTT_SERVER_PORT        "1883"

const char OPEN = 1;
const char CLOSE = 0;

int sock_id = 0;

void sensor_init(void) {
    Init_E53_IA1();
}

void sensor_read(void) {
    E53_IA1_Read_Data();
}

int light_intensity_low() {
    return E53_IA1_Data.Lux < (float)20.0 && E53_IA1_Data.LightMode == CLOSE;
}

int light_intensity_high() {
    return E53_IA1_Data.Lux > (float)20.0 && E53_IA1_Data.LightMode == OPEN;
}

int temperature_too_high() {
    return E53_IA1_Data.Lux > (float)20.0 && E53_IA1_Data.LightMode == OPEN;
    // return E53_IA1_Data.Temperature > (float)30.0 && E53_IA1_Data.MotorMode == OPEN;
}

void turnon_fan() {
    motor_control(OPEN);
}

void turnoff_fan() {
    motor_control(CLOSE);
}

void turnon_light() {
    light_control(OPEN);
}

void turnoff_light() {
    light_control(CLOSE);
}

void print_sensor_data() {
    printf("{\"temperature\":%.2f,\"humidity\":%.2f,\"light_intensity\":%.2f,\"motor\":%d,\"light\":%d}\n",
           E53_IA1_Data.Temperature,
           E53_IA1_Data.Humidity,
           E53_IA1_Data.Lux,
           E53_IA1_Data.MotorMode,
           E53_IA1_Data.LightMode);
}

int parse_data(uint8_t *data) {
    //{"motor":0,"light":1}
    cJSON* cjson = cJSON_Parse((char *)data);
    cJSON *cjsonret = NULL;
    int motor;
    int light;
    if(cjson == NULL) {
        printf("json pack into cjson error...");
        return -1;
    } else {
        cJSON_Print(cjson);
    }
    cjsonret = NULL;
    cjsonret = cJSON_GetObjectItem(cjson, "motor");
    if(cjsonret != NULL) {
        motor = cjsonret->valueint;
        printf("motor=%d\n", motor);
        if(motor == 1) {
            //power on relay
            turnon_fan();
            // motor_control(OPEN);
        } else if(motor == 0) {
            //power off relay
            motor_control(CLOSE);
        }
    }
    cjsonret = NULL;
    cjsonret = cJSON_GetObjectItem(cjson, "light");
    if(cjsonret != NULL) {
        light = cjsonret->valueint;
        printf("light=%d\n", light);
        if(light == 1) {
            //power on relay
            light_control(OPEN);
        } else if(light == 0) {
            //power off relay
            light_control(CLOSE);
        }
    }
    cJSON_Delete(cjson);
    return 0;
}

int receive_data(void) {
    static int count = 1;
    uint8_t read_data[100] = {0};
    int read_len;
    mqtt_sub_opt_t sub_opt;
    sub_opt.count = 1;
    sub_opt.dup = 0;
    sub_opt.id = 1;
    sub_opt.req_qos = 0;
    sub_opt.topic = MQTT_SUBSCRIBE_TOPIC;

    printf("receive: \r\n");
    read_len = tos_mqtt_receive(sub_opt.topic, strlen(sub_opt.topic), read_data, sizeof(read_data));
    if (read_len >= 0) {
        printf("MQTT count: %d\r\n", count++);
        printf("---------->topic: %s, payload: %s, payload_len: %d\n", sub_opt.topic, read_data, read_len);
        parse_data(read_data);
    }
    return 0;
}

int publish_data(void) {
    mqtt_pub_opt_t pub_opt;
    pub_opt.dup = 0;
    pub_opt.qos = 0;
    pub_opt.retained = 0;
    pub_opt.id = 0;
    pub_opt.topic = MQTT_PUBLISH_TOPIC;

    char buffer[256] = {0};
    snprintf(buffer, sizeof(buffer),
             "{\"type\":\"update\",\"state\":{\"desired\":null,\"reported\":{\"temperature\":%.2f,\"humidity\":%.2f,\"light_intensity\":%.2f,\"motor\":%d,\"light\":%d}},\"version\":0,\"clientToken\":\"clientToken\"}",
             E53_IA1_Data.Temperature,
             E53_IA1_Data.Humidity,
             E53_IA1_Data.Lux,
             E53_IA1_Data.MotorMode,
             E53_IA1_Data.LightMode);

    pub_opt.payload = (unsigned char *)buffer;
    pub_opt.payload_len = strlen(buffer);
    printf("\r\nreport--->topic:%s\r\ndata(%d):%s\r\n", pub_opt.topic, pub_opt.payload_len, pub_opt.payload);
    if (tos_mqtt_publish(sock_id, &pub_opt) != 0) {
        printf("publish failed!!!\n");
    }
    return 0;
}

int network_init() {
#ifdef USE_ESP8266
    extern int esp8266_sal_init(hal_uart_port_t uart_port);
    extern int esp8266_join_ap(const char *ssid, const char *pwd);
    esp8266_sal_init(HAL_UART_PORT_0);
    int ret = esp8266_join_ap("Tencent-StaffWiFi", "");
    //int ret = esp8266_join_ap("Tencent-StaffWiFi", "");
    //int ret = esp8266_join_ap("Tencent-GuestWiFi", "252901");
    return ret;
#endif
}

int mqtt_connect(void) {
    mqtt_con_opt_t con_opt;
    con_opt.keep_alive_interval = 2000;
    con_opt.cleansession = 1;
    con_opt.username = MQTT_USR_NAME;
    con_opt.password = MQTT_PASSWORD;
    con_opt.client_id = MQTT_CLIENT_ID;

    int try = 0;
    while(try++ < 10) {
        sock_id = tos_mqtt_connect(MQTT_SERVER_IP, MQTT_SERVER_PORT, &con_opt);
        if (sock_id != -1) {
            printf("tos_mqtt_connect success. sock_id  %d \r\n", sock_id);
            return 0;
        }
        transport_close(sock_id);
        printf("tos_mqtt_connect fail. sock_id  %d \r\n", sock_id);
        osDelay(2000);
    }
    return -1;
}

int mqtt_subscribe(void) {
    mqtt_sub_opt_t sub_opt;
    sub_opt.count = 1;
    sub_opt.dup = 0;
    sub_opt.id = 1;
    sub_opt.req_qos = 0;
    sub_opt.topic = MQTT_SUBSCRIBE_TOPIC;

    int try = 0;
    while (try++ < 10) {
        if (tos_mqtt_subscribe(sock_id, &sub_opt) == 0) {
            printf("subscribe topic:%s success\n", sub_opt.topic);
            return 0;
        }
        osDelay(2000);
    }
    printf("subscribe topic:%s fail\n", sub_opt.topic);

    return -1;
}

void qcloud_agent(void) {
    // ????????????????????????????????????
    int ret = network_init();
    if (ret < 0) {
        printf("network_init fail\n");
        return;
    }

    // ?????????IoT hub???MQTT??????
    ret = mqtt_connect();
    if (ret < 0) {
        printf("mqtt_connect fail\n");
        return;
    }

    // ?????????????????????????????????????????????
    ret = mqtt_subscribe();
    if (ret < 0) {
        printf("mqtt_subscribe fail\n");
        return;
    }

    sensor_init();
    while(1) {
        sensor_read();
        // ????????????????????? 
        publish_data();
        // ????????????????????????
        receive_data();
        osDelay(1000);
    }
}


void one_loop_task(void *arg) {
    sensor_init();
    while(1) {
        sensor_read();
        print_sensor_data();
        // ?????????????????? => ???????????????
        if (light_intensity_low()) { 
            turnon_light();  // ????????????????????????   
        // ?????????????????? => ???????????????    
        } else if (light_intensity_high()) {  
            turnoff_light();
        }

        // ??????????????????
        printf("delay 5 senconds \n");
        osDelay(5000);
    }
}

void monitor_task(void) {
    sensor_init();
    while(1) {
        sensor_read();
        // ?????????????????? => ???????????????
        if (light_intensity_low()) {
            turnon_light();
            turnon_fan();
        // ?????????????????? => ??????????????? 
        } else if (light_intensity_high()) {
            turnoff_light();
            turnoff_fan();
        }
        osDelay(100);
    }
}

void normal_task() {
    while(1) {
        printf("do something for 5 senconds \n");
        print_sensor_data();
        osDelay(5000);
    }
}

void network_test_task() {
    // ?????????WiFi???????????????AP
    int ret = network_init();
    if (ret < 0) {
        printf("network_init fail\n");
        return;
    }

    // ??????SAL??????????????????Socket??????
    int socket_id = -1;
    uint8_t recv_data[128];
    socket_id = tos_sal_module_connect("45.40.201.56", "8080", TOS_SAL_PROTO_TCP);
    if (socket_id == -1) {
        printf("TCP connect failed\r\n");
        return;
    } else {
        printf("TCP connect success! fd: %d\n", socket_id);
    }

    // ??????SAL????????????????????????
    while (1) {
        tos_sal_module_send(socket_id, (const void *)"Hello TencentOS tiny!\r\n",
            strlen("Hello TencentOS tiny!\r\n"));

        int recv_len = tos_sal_module_recv_timeout(socket_id, recv_data, sizeof(recv_data), 8000);
		if (recv_len < 0) {
			printf("receive error\n\r");
		} else if (recv_len == 0) {
		    printf("receive none\n");
		} else {
			recv_data[recv_len] = 0;
			printf("receive len: %d\n recv_data: %s\n", recv_len, recv_data);
		}

        tos_sleep_ms(2000);
    }
}

// ????????????
osThreadDef(network_test_task, osPriorityNormal, 1, 1024);

// ????????????
osThreadDef(qcloud_agent, osPriorityNormal, 1, 8192);

// ?????????????????? 
osThreadDef(one_loop_task, osPriorityNormal, 1, 1024);

// 1. ??????????????????
#define MONITOR_STK_SIZE 1024
osThreadDef(monitor_task, osPriorityHigh, 1, MONITOR_STK_SIZE);

// 2. ?????????????????????
#define NORMAL_STK_SIZE 512
osThreadDef(normal_task, osPriorityNormal, 1, NORMAL_STK_SIZE);

void helloworld(void *arg)
{
	while(1) {
		printf("helloworld\n");
		osDelay(1000);
	}
}

#define TASK2_STK_SIZE          512
osThreadDef(helloworld, osPriorityNormal, 1, TASK2_STK_SIZE);

void application_entry(void *arg) {    
    osThreadCreate(osThread(qcloud_agent), NULL);
}

/*
// ????????????
osThreadCreate(osThread(helloworld), NULL);

// ??????????????????
sensor_init();
while(1) {
    sensor_read();
    print_sensor_data();
    
    // ?????????????????? => ???????????????
    if (light_intensity_low()) { 
        turnon_light();  // ????????????????????????   
    // ?????????????????? => ???????????????    
    } else if (light_intensity_high()) {  
        turnoff_light();
    }

    // ??????????????????
    printf("do something for 5 senconds \n");
    osDelay(5000);
}


// ?????????????????????????????????
// ??????????????????: ?????????????????????????????????????????????
osThreadCreate(osThread(monitor_task), NULL);
// ??????????????????????????????????????????
osThreadCreate(osThread(normal_task), NULL);

// ????????????
osThreadCreate(osThread(network_test_task), NULL);

// ????????????
osThreadCreate(osThread(qcloud_agent), NULL);

*/


