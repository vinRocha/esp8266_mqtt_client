/*
 * FreeRTOS V202212.01
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * Demo for showing use of the managed MQTT API.
 *
 * The example shown below uses this API to create MQTT messages and
 * send them over the connection established using FreeRTOS sockets.
 * The example is single threaded and uses statically allocated memory;
 *
 * !!! NOTE !!!
 * This MQTT demo does not authenticate the server nor the client.
 * Hence, this demo should not be used as production ready code.
 *
 * Also see https://www.freertos.org/mqtt/mqtt-agent-demo.html? for an
 * alternative run time model whereby coreMQTT runs in an autonomous
 * background agent task.  Executing the MQTT protocol in an agent task
 * removes the need for the application writer to explicitly manage any MQTT
 * state or call the MQTT_ProcessLoop() API function. Using an agent task
 * also enables multiple application tasks to more easily share a single
 * MQTT connection.
 */

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <iostream>
#include <chrono>
#include <pthread.h>

#include "core_mqtt.h"
#include "transport_esp8266.h"

//MQTT Client configuration:
#define configMQTT_BROKER_ENDPOINT                "192.168.0.235"
#define configMQTT_BROKER_PORT                    "1883"
#define configNETWORK_BUFFER_SIZE                 128U
#define configCLIENT_IDENTIFIER                   "esp8266-linux_client"
#define configRETRY_MAX_ATTEMPTS                  5U
#define configRETRY_MAX_BACKOFF_DELAY_MS          1000U
#define configTOPIC_PREFIX     	                  "/mqtt/test"
#define configTOPIC_COUNT                         3
#define configTOPIC_BUFFER_SIZE                   100U
#define configMESSAGE                             "Hello World from ESP8266!"
#define configKEEP_ALIVE_TIMEOUT_S                60U
#define configTRANSPORT_SEND_RECV_TIMEOUT_MS      200U
#define configOUTGOING_PUBLISH_RECORD_LEN         10U
#define configINCOMING_PUBLISH_RECORD_LEN         10U

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer to the type of your desired transport.
 * When using multiple transports in the same compilation unit, define this pointer as void *.
 *
 * @note Transport stacks are defined in FreeRTOS-Plus/Source/Application-Protocols/network_transport.
 */
//Not needed for transport_esp8266
struct NetworkContext {
};

//Varible to control run_thread
static bool stop = false;

/**
 * @brief Static buffer used to hold MQTT messages being sent and received.
 */
static uint8_t ucSharedBuffer[ configNETWORK_BUFFER_SIZE ];

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static std::chrono::system_clock::time_point ulGlobalEntryTimeMs;

/**
 * @brief Packet Identifier generated when Publish request was sent to the broker;
 * it is used to match received Publish ACK to the transmitted Publish packet.
 */
static uint16_t usPublishPacketIdentifier;

/**
 * @brief Packet Identifier generated when Subscribe request was sent to the broker;
 * it is used to match received Subscribe ACK to the transmitted Subscribe packet.
 */
static uint16_t usSubscribePacketIdentifier;

/**
 * @brief Packet Identifier generated when Unsubscribe request was sent to the broker;
 * it is used to match received Unsubscribe response to the transmitted Unsubscribe
 * request.
 */
static uint16_t usUnsubscribePacketIdentifier;

/**
 * @brief A pair containing a topic filter and its SUBACK status.
 */
typedef struct topicFilterContext
{
    uint8_t pcTopicFilter[ configTOPIC_BUFFER_SIZE ];
    MQTTSubAckStatus_t xSubAckStatus;
} topicFilterContext_t;

/**
 * @brief An array containing the context of a SUBACK; the SUBACK status
 * of a filter is updated when the event callback processes a SUBACK.
 */
static topicFilterContext_t xTopicFilterContext[ configTOPIC_COUNT ];

/** @brief Static buffer used to hold MQTT messages being sent and received. */
static MQTTFixedBuffer_t xBuffer =
{
    ucSharedBuffer,
    configNETWORK_BUFFER_SIZE
};

/**
 * @brief Array to track the outgoing publish records for outgoing publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pOutgoingPublishRecords[ configOUTGOING_PUBLISH_RECORD_LEN ];

/**
 * @brief Array to track the incoming publish records for incoming publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pIncomingPublishRecords[ configINCOMING_PUBLISH_RECORD_LEN ];

static void initialize();
static void loop();
static void *run_thread(void *args);

/**
 * @brief Sends an MQTT Connect packet over the already connected TLS over TCP connection.
 *
 * @param[in, out] pxMQTTContext MQTT context pointer.
 * @param[in] xNetworkContext network context.
 */
static void prvCreateMQTTConnectionWithBroker( MQTTContext_t * pxMQTTContext,
                                               NetworkContext_t * pxNetworkContext );

/**
 * @brief Function to update variable #Context with status
 * information from Subscribe ACK. Called by the event callback after processing
 * an incoming SUBACK packet.
 *
 * @param[in] Server response to the subscription request.
 */
static void prvUpdateSubAckStatus( MQTTPacketInfo_t * pxPacketInfo );

/**
 * @brief Subscribes to the topic as specified in mqttexampleTOPIC at the top of
 * this file. In the case of a Subscribe ACK failure, then subscription is
 * retried using an exponential backoff strategy with jitter.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 */
static void prvMQTTSubscribeWithBackoffRetries( MQTTContext_t * pxMQTTContext );

/**
 * @brief Publishes a message configMESSAGE on mqttexampleTOPIC topic.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 */
static void prvMQTTPublishToTopics( MQTTContext_t * pxMQTTContext );

/**
 * @brief Unsubscribes from the previously subscribed topic as specified
 * in mqttexampleTOPIC.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 */
static void prvMQTTUnsubscribeFromTopics( MQTTContext_t * pxMQTTContext );

/**
 * @brief The timer query function provided to the MQTT context.
 *
 * @return Time in milliseconds.
 */
static uint32_t prvGetTimeMs( void );

/**
 * @brief Process a response or ack to an MQTT request (PING, PUBLISH,
 * SUBSCRIBE or UNSUBSCRIBE). This function processes PINGRESP, PUBACK,
 * PUBREC, PUBREL, PUBCOMP, SUBACK, and UNSUBACK.
 *
 * @param[in] pxIncomingPacket is a pointer to structure containing deserialized
 * MQTT response.
 * @param[in] usPacketId is the packet identifier from the ack received.
 */
static void prvMQTTProcessResponse( MQTTPacketInfo_t * pxIncomingPacket,
                                    uint16_t usPacketId );

/**
 * @brief Process incoming Publish message.
 *
 * @param[in] pxPublishInfo is a pointer to structure containing deserialized
 * Publish message.
 */
static void prvMQTTProcessIncomingPublish( MQTTPublishInfo_t * pxPublishInfo );

/**
 * @brief The application callback function for getting the incoming publishes,
 * incoming acks, and ping responses reported from the MQTT library.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 * @param[in] pxPacketInfo Packet Info pointer for the incoming packet.
 * @param[in] pxDeserializedInfo Deserialized information from the incoming packet.
 */
static void prvEventCallback( MQTTContext_t * pxMQTTContext,
                              MQTTPacketInfo_t * pxPacketInfo,
                              MQTTDeserializedInfo_t * pxDeserializedInfo );

/**
 * @brief Call #MQTT_ProcessLoop in a loop for the duration of a timeout or
 * #MQTT_ProcessLoop returns a failure.
 *
 * @param[in] pMqttContext MQTT context pointer.
 * @param[in] ulTimeoutMs Duration to call #MQTT_ProcessLoop for.
 *
 * @return Returns the return value of the last call to #MQTT_ProcessLoop.
 */
static MQTTStatus_t prvProcessLoopWithTimeout( MQTTContext_t * pMqttContext,
                                               uint32_t ulTimeoutMs );

/**
 * @brief Initialize the topic filter string and SUBACK buffers.
 */
static void prvInitializeTopicBuffers( void );


int main(int argc, char * argv[]) {

  initialize();
  pthread_t run_thread_id;
  if (pthread_create(run_thread_id, NULL, &run_thread, NULL)) {
    perror("Not able to spawn run thread.");
    exit(errno);
  }

  std::cout << "Press enter to exit..." << std::endl;
  getchar();
  stop = true;
  pthread_join(run_thread_id, NULL);
  return 0;
}

void initialize() {
  ulGlobalEntryTimeMs = std::chrono::system_clock::now();
}

void loop() {
  static uint32_t ulPublishCount = 0, ulTopicCount = 0;
  static const uint32_t ulMaxPublishCount = 5;
  static NetworkContext_t xNetworkContext = {0};
  static MQTTContext_t xMQTTContext = {0};
  static MQTTStatus_t xMQTTStatus;
  static esp8266TransportStatus_t xNetworkStatus;

  std::cout << "----------STARTING DEMO----------" << std::endl;
  prvInitializeTopicBuffers();
  xNetworkStatus = esp8266AT_Connect(configMQTT_BROKER_ENDPOINT, configMQTT_BROKER_PORT);
  if (xNetworkStatus != ESP8266_TRANSPORT_SUCCESS) {
    std::cerr << "Failed to initialise network." << std::endl;
    exit(-1);
  }

  std::cout << "Creating an MQTT connection to " << configMQTT_BROKER_ENDPOINT "." << std::endl;
  prvCreateMQTTConnectionWithBroker(&xMQTTContext, &xNetworkContext);

  /**************************** Subscribe. ******************************/

  /* If the server rejected the subscription request, attempt to resubscribe to the
   * topic. Attempts are made according to the exponential backoff retry strategy
   * implemented in BackoffAlgorithm. */
  prvMQTTSubscribeWithBackoffRetries(&xMQTTContext);

  /**************************** Publish and Keep-Alive Loop. ******************************/

  /* Publish messages with QoS2, and send and process keep-alive messages. */
  for (ulPublishCount = 0; ulPublishCount < ulMaxPublishCount; ulPublishCount++) {
    prvMQTTPublishToTopics(&xMQTTContext);

    /* Process incoming publish echo. Since the application subscribed and published
     * to the same topic, the broker will send the incoming publish message back
     * to the application. */
    LogInfo(("Attempt to receive publishes from broker.\r\n"));
    xMQTTStatus = prvProcessLoopWithTimeout(&xMQTTContext, configPROCESS_LOOP_TIMEOUT_MS);
    configASSERT(xMQTTStatus == MQTTSuccess);

    /* Leave connection idle for some time. */
    LogInfo(("Keeping Connection Idle...\r\n\r\n"));
    vTaskDelay(mqttexampleDELAY_BETWEEN_PUBLISHES_TICKS);
  }

  /************************ Unsubscribe from the topic. **************************/

  prvMQTTUnsubscribeFromTopics(&xMQTTContext);

  /* Process incoming UNSUBACK packet from the broker. */
  xMQTTStatus = prvProcessLoopWithTimeout(&xMQTTContext, configPROCESS_LOOP_TIMEOUT_MS);
  configASSERT(xMQTTStatus == MQTTSuccess);

  /**************************** Disconnect. ******************************/

  /* Send an MQTT DISCONNECT packet over the already-connected TLS over TCP connection.
   * There is no corresponding response expected from the broker. After sending the
   * disconnect request, the client must close the network connection. */
  LogInfo(("Disconnecting the MQTT connection with %s.\r\n", democonfigMQTT_BROKER_ENDPOINT));
  xMQTTStatus = MQTT_Disconnect(&xMQTTContext);
  configASSERT(xMQTTStatus == MQTTSuccess);

  /* Close the network connection.  */
  Plaintext_FreeRTOS_Disconnect(&xNetworkContext);

  /* Reset SUBACK status for each topic filter after completion of the subscription request cycle. */
  for(ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++) {
      xTopicFilterContext[ulTopicCount].xSubAckStatus = MQTTSubAckFailure;
  }

  /* Wait for some time between two iterations to ensure that we do not
   * bombard the broker. */
  LogInfo(("prvMQTTDemoTask() completed an iteration successfully. Total free heap is %u.\r\n", xPortGetFreeHeapSize()));
  LogInfo(("Demo completed successfully.\r\n"));
  LogInfo(("-------DEMO FINISHED-------\r\n"));
  LogInfo(("Short delay before starting the next iteration.... \r\n\r\n"));
  vTaskDelay(mqttexampleDELAY_BETWEEN_DEMO_ITERATIONS_TICKS);
}

void *run_thread(void *args) {
}
