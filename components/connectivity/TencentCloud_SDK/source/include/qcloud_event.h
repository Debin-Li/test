#ifndef _QCLOUD_EVENT_H_
#define  _QCLOUD_EVENT_H_

#define QCLOUD_EVENT_TOKEN_MAX      (32)

#define EVENT_FIELD_POST    "event_post"
#define EVENT_FIELD_POSTS   "events_post"
#define EVENT_FIELD_REPLY   "event_reply"

#define EVENT_REPLY_FIELD_CODE          "code"
#define EVENT_REPLY_FIELD_STATUS        "status"

typedef void (*event_reply_handler_fn_t)(void *client, mqtt_incoming_msg_t *msg);

typedef struct qcloud_event_reply_st {
    qcloud_list_t               list;

    char                        client_token[QCLOUD_EVENT_TOKEN_MAX];  // 标识该请求的clientToken字段
    osal_timer_t                timer;  // 请求超时定时器
    event_reply_handler_fn_t    handler;    // 事件上报回复回调
} qcloud_event_reply_t;

typedef enum event_sync_state_en {
    QCLOUD_EVENT_SYNC_STATE_NONE,
    QCLOUD_EVENT_SYNC_STATE_PENDACK,
    QCLOUD_EVENT_SYNC_STATE_SUCCESS,
    QCLOUD_EVENT_SYNC_STATE_TIMEOUT,
    QCLOUD_EVENT_SYNC_STATE_NACK,
} event_sync_state_t;

typedef struct qcloud_event_st {
    char                   *event_name; // event name
    char                   *type;   // event type
    uint32_t                timestamp;  // timestamp
    uint8_t                 event_payload_count;    // count of event_payload
    shadow_dev_property_t  *event_payload;  // payload of the event
} qcloud_event_t;

typedef struct qcloud_event_client_st {
    void                   *reply_list_lock;
    qcloud_list_t           reply_list;

    char                    down_topic[QCLOUD_MQTT_TOPIC_SIZE_MAX];
    char                    up_topic[QCLOUD_MQTT_TOPIC_SIZE_MAX];

    event_sync_state_t      sync_state;

    qcloud_shadow_client_t *shadow_client;
} qcloud_event_client_t;

__QCLOUD_API__ qcloud_err_t qcloud_event_client_create(qcloud_event_client_t *client, qcloud_shadow_client_t *shadow_client, qcloud_device_t *device);

__QCLOUD_API__ qcloud_err_t qcloud_event_client_destroy(qcloud_event_client_t *client);

/**
 * @brief �¼��ϱ��������¼����飬SDK����¼���json��ʽ��װ
 * @param pClient shadow ʵ��ָ��
 * @param pJsonDoc    ���ڹ���json��ʽ�ϱ���Ϣ��buffer
 * @param sizeOfBuffer    ���ڹ���json��ʽ�ϱ���Ϣ��buffer��С
 * @param event_count     ���ϱ����¼�����
 * @param pEventArry	  ���ϱ����¼�����ָ
 * @param replyCb	  �¼��ظ���Ϣ�Ļص�
 * @return @see IoT_Error_Code
 */
__QCLOUD_API__ qcloud_err_t qcloud_event_client_post(qcloud_event_client_t *client,
                                                                        char *json_doc,
                                                                        size_t json_doc_size,
                                                                        int event_count,
                                                                        qcloud_event_t *events[],
                                                                        event_reply_handler_fn_t handler);

/**
 * @brief �¼��ϱ����û������ѹ����õ��¼���json��ʽ��SDK�����¼�ͷ�����ϱ�
 * @param pClient shadow ʵ��ָ��
 * @param pJsonDoc    ���ڹ���json��ʽ�ϱ���Ϣ��buffer
 * @param sizeOfBuffer    ���ڹ���json��ʽ�ϱ���Ϣ��buffer��С
 * @param pEventMsg     ���ϱ����¼�json��Ϣ
 *  json�¼���ʽ��
 *  �����¼���
 *	 {"method": "event_post",
 *		"clientToken": "123",
 *		"version": "1.0",
 *		"eventId": "PowerAlarm",
 *		"type": "fatal",
 *		"timestamp": 1212121221,
 *		"params": {
 *			"Voltage": 2.8,
 *			"Percent": 20
 *		}
 *	}
 *
 *  ����¼���
 *	 {
 *		 "eventId": "PowerAlarm",
 *		 "type": "fatal",
 *		 "timestamp": 1212121221,
 *		 "params": {
 *			 "Voltage": 2.8,
 *			 "Percent": 20
 *		 }
 *	 },
 *	 {
 *		 "name": "PowerAlarm",
 *		 "type": "fatal",
 *		 "timestamp": 1212121223,
 *		 "params": {
 *			 "Voltage": 2.1,
 *			 "Percent": 10
 *		 }
 *	 },
 *   ....
 *
 * @param replyCb	  �¼��ظ���Ϣ�Ļص�
 * @return @see IoT_Error_Code
 */
__QCLOUD_API__ qcloud_err_t qcloud_event_client_post_raw(qcloud_event_client_t *client,
                                                                            char *json_doc,
                                                                            size_t json_doc_size,
                                                                            char *event_msg,
                                                                            event_reply_handler_fn_t handler);

__QCLOUD_INTERNAL__ qcloud_err_t event_json_node_add(char *json_doc, size_t json_doc_size, const char *key, void *data, json_data_type_t type);

__QCLOUD_INTERNAL__ int event_json_return_code_parse(char *json_doc, int32_t *return_code);

__QCLOUD_INTERNAL__ int event_json_status_parse(char *json_doc, char **return_status);

#endif

