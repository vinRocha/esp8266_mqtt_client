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
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <cassert>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <pthread.h>

#include "core_mqtt.h"
#include "transport_esp8266.h"

#define configASSERT assert

//MQTT Client configuration:
#define configMQTT_BROKER_ENDPOINT                "192.168.0.235"
#define configMQTT_BROKER_PORT                    "1883"
#define configNETWORK_BUFFER_SIZE                 128U
#define configCLIENT_IDENTIFIER                   "esp8266-linux_client"
#define configRETRY_MAX_ATTEMPTS                  5U
#define configRETRY_MAX_BACKOFF_DELAY_MS          1000U
#define configMAX_PUBLUSH_COUNT                   3
#define configTOPIC_PREFIX     	                  "/mqtt/test"
#define configTOPIC_COUNT                         1
#define configTOPIC_BUFFER_SIZE                   100U
#define configMESSAGE                             "Hello World from ESP8266!"
#define configKEEP_ALIVE_TIMEOUT_S                40U
#define configOUTGOING_PUBLISH_RECORD_LEN         16U
#define configINCOMING_PUBLISH_RECORD_LEN         16U
#define configPROCESS_LOOP_TIMEOUT_MS             1000U
#define configDELAY_BETWEEN_PUBLISHES_S           1
#define configDELAY_BETWEEN_DEMO_ITERATIONS_S     3
#define configCONNACK_RECV_TIMEOUT_MS             2000U

/*
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer to the type of your desired transport.
 * When using multiple transports in the same compilation unit, define this pointer as void *.
 *
 * @note Transport stacks are defined in FreeRTOS-Plus/Source/Application-Protocols/network_transport.
 *
 * Not needed for transport_esp8266
 */

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
  if (pthread_create(&run_thread_id, NULL, &run_thread, NULL)) {
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
  static const uint32_t ulMaxPublishCount = configMAX_PUBLUSH_COUNT;
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
  prvCreateMQTTConnectionWithBroker(&xMQTTContext, NULL);

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
    std::cout << "Attempt to receive publishes from broker" << std::endl;
    xMQTTStatus = prvProcessLoopWithTimeout(&xMQTTContext, configPROCESS_LOOP_TIMEOUT_MS);
    configASSERT(xMQTTStatus == MQTTSuccess);

    /* Leave connection idle for some time. */
    std::cout << "Keeping Connection Idle..." << std::endl;
    sleep(configDELAY_BETWEEN_PUBLISHES_S);
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
  std::cout << "Disconnecting the MQTT connection with " << configMQTT_BROKER_ENDPOINT \
    << "." << std::endl;
  xMQTTStatus = MQTT_Disconnect(&xMQTTContext);
  configASSERT(xMQTTStatus == MQTTSuccess);

  /* Close the network connection.  */
  esp8266AT_Disconnect();

  /* Reset SUBACK status for each topic filter after completion of the subscription request cycle. */
  for(ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++) {
      xTopicFilterContext[ulTopicCount].xSubAckStatus = MQTTSubAckFailure;
  }

  /* Wait for some time between two iterations to ensure that we do not
   * bombard the broker. */
  std::cout << "prvMQTTDemoTask() completed an iteration successfully." << std::endl;
  std::cout << "Demo completed successfully." << std::endl;
  std::cout << "-------DEMO FINISHED-------"  << std::endl;
  std::cout << "Short delay before starting the next iteration...." << std::endl;
  sleep(configDELAY_BETWEEN_DEMO_ITERATIONS_S);
}

void *run_thread(void *args) {

  while(!stop) {
    loop();
  }
  return NULL;
}

void prvCreateMQTTConnectionWithBroker(MQTTContext_t *pxMQTTContext, NetworkContext_t *pxNetworkContext) {
  MQTTStatus_t xResult;
  MQTTConnectInfo_t xConnectInfo;
  bool xSessionPresent;
  TransportInterface_t xTransport;

  /* Fill in Transport Interface send and receive function pointers. */
  xTransport.pNetworkContext = NULL;
  xTransport.send = esp8266AT_send;
  xTransport.recv = esp8266AT_recv;
  xTransport.writev = NULL;

  /* Initialize MQTT library. */
  xResult = MQTT_Init( pxMQTTContext, &xTransport, prvGetTimeMs, prvEventCallback, &xBuffer );
  configASSERT( xResult == MQTTSuccess );
  xResult = MQTT_InitStatefulQoS( pxMQTTContext,
                                  pOutgoingPublishRecords,
                                  configOUTGOING_PUBLISH_RECORD_LEN,
                                  pIncomingPublishRecords,
                                  configINCOMING_PUBLISH_RECORD_LEN );
  configASSERT( xResult == MQTTSuccess );

  /* Some fields are not used in this demo so start with everything at 0. */
  ( void ) memset( ( void * ) &xConnectInfo, 0x00, sizeof( xConnectInfo ) );

  /* Start with a clean session i.e. direct the MQTT broker to discard any
   * previous session data. Also, establishing a connection with clean session
   * will ensure that the broker does not store any data when this client
   * gets disconnected. */
  xConnectInfo.cleanSession = true;

  /* The client identifier is used to uniquely identify this MQTT client to
   * the MQTT broker. In a production device the identifier can be something
   * unique, such as a device serial number. */
  xConnectInfo.pClientIdentifier = configCLIENT_IDENTIFIER;
  xConnectInfo.clientIdentifierLength = ( uint16_t ) strlen( configCLIENT_IDENTIFIER );

  /* Set MQTT keep-alive period. If the application does not send packets at an interval less than
   * the keep-alive period, the MQTT library will send PINGREQ packets. */
  xConnectInfo.keepAliveSeconds = configKEEP_ALIVE_TIMEOUT_S;

  /* Send MQTT CONNECT packet to broker. LWT is not used in this demo, so it
   * is passed as NULL. */
  xResult = MQTT_Connect( pxMQTTContext,
                          &xConnectInfo,
                          NULL,
                          configCONNACK_RECV_TIMEOUT_MS,
                          &xSessionPresent );
  configASSERT( xResult == MQTTSuccess );

  /* Successfully established and MQTT connection with the broker. */
  std::cout << "An MQTT connection is established with " << configMQTT_BROKER_ENDPOINT \
        << "." << std::endl;
}
/*-----------------------------------------------------------*/

void prvUpdateSubAckStatus(MQTTPacketInfo_t *pxPacketInfo) {
  MQTTStatus_t xResult = MQTTSuccess;
  uint8_t *pucPayload = NULL;
  size_t ulSize = 0;
  uint32_t ulTopicCount = 0U;

  xResult = MQTT_GetSubAckStatusCodes(pxPacketInfo, &pucPayload, &ulSize);

  /* MQTT_GetSubAckStatusCodes always returns success if called with packet info
   * from the event callback and non-NULL parameters. */
  configASSERT(xResult == MQTTSuccess);

  for (ulTopicCount = 0; ulTopicCount < ulSize; ulTopicCount++) {
      xTopicFilterContext[ulTopicCount].xSubAckStatus = pucPayload[ulTopicCount];
  }
}
/*-----------------------------------------------------------*/

void prvMQTTSubscribeWithBackoffRetries( MQTTContext_t * pxMQTTContext ) {
  MQTTStatus_t xResult = MQTTSuccess;
  //BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
  //BackoffAlgorithmContext_t xRetryParams;
  int counter = configRETRY_MAX_ATTEMPTS;
  uint16_t usNextRetryBackOff = 500U;
  MQTTSubscribeInfo_t xMQTTSubscription[ configTOPIC_COUNT ];
  bool xFailedSubscribeToTopic = false;
  uint32_t ulTopicCount = 0U;

  /* Some fields not used by this demo so start with everything at 0. */
  ( void ) memset( ( void * ) &xMQTTSubscription, 0x00, sizeof( xMQTTSubscription ) );

  /* Get a unique packet id. */
  usSubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );

  /* Populate subscription list. */
  for(ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++)
  {
      xMQTTSubscription[ ulTopicCount ].qos = MQTTQoS2;
      xMQTTSubscription[ ulTopicCount ].pTopicFilter = xTopicFilterContext[ ulTopicCount ].pcTopicFilter;
      xMQTTSubscription[ ulTopicCount ].topicFilterLength = ( uint16_t ) strlen( xTopicFilterContext[ ulTopicCount ].pcTopicFilter );
  }

  do
  {
      /* The client is now connected to the broker. Subscribe to the topic
       * as specified in mqttexampleTOPIC at the top of this file by sending a
       * subscribe packet then waiting for a subscribe acknowledgment (SUBACK).
       * This client will then publish to the same topic it subscribed to, so it
       * will expect all the messages it sends to the broker to be sent back to it
       * from the broker. This demo uses QOS2 in Subscribe, therefore, the Publish
       * messages received from the broker will have QOS2. */
      xResult = MQTT_Subscribe( pxMQTTContext,
                                xMQTTSubscription,
                                sizeof( xMQTTSubscription ) / sizeof( MQTTSubscribeInfo_t ),
                                usSubscribePacketIdentifier );
      configASSERT( xResult == MQTTSuccess );

      for(ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++)
      {
        std::cout << "SUBSCRIBE sent for topic " << xTopicFilterContext[ ulTopicCount ].pcTopicFilter \
          << " to broker." << std::endl;
      }

      /* Process incoming packet from the broker. After sending the subscribe, the
       * client may receive a publish before it receives a subscribe ack. Therefore,
       * call generic incoming packet processing function. Since this demo is
       * subscribing to the topic to which no one is publishing, probability of
       * receiving Publish message before subscribe ack is zero; but application
       * must be ready to receive any packet.  This demo uses the generic packet
       * processing function everywhere to highlight this fact. */
      xResult = prvProcessLoopWithTimeout( pxMQTTContext, configPROCESS_LOOP_TIMEOUT_MS );
      configASSERT( xResult == MQTTSuccess );

      /* Reset flag before checking suback responses. */
      xFailedSubscribeToTopic = false;

      /* Check if recent subscription request has been rejected. #xTopicFilterContext is updated
       * in the event callback to reflect the status of the SUBACK sent by the broker. It represents
       * either the QoS level granted by the server upon subscription, or acknowledgement of
       * server rejection of the subscription request. */
      for( ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++ )
      {
          if( xTopicFilterContext[ ulTopicCount ].xSubAckStatus == MQTTSubAckFailure )
          {
              xFailedSubscribeToTopic = true;

              /* Generate a random number and calculate backoff value (in milliseconds) for
               * the next connection retry.
               * Note: It is recommended to seed the random number generator with a device-specific
               * entropy source so that possibility of multiple devices retrying failed network operations
               * at similar intervals can be avoided. */

              counter--;
              if( !counter )
              {
                std::cout << "Server rejected subscription request. All retry attempts have exhausted. Topic=" \
                  << xTopicFilterContext[ ulTopicCount ].pcTopicFilter << "." << std::endl;
              }
              else
              {
                std::cout << "Server rejected subscription request. Attempting to re-subscribe to topic" \
                  << xTopicFilterContext[ ulTopicCount ].pcTopicFilter << "." << std::endl;
                  /* Backoff before the next re-subscribe attempt. */
                  usleep( 1000 * usNextRetryBackOff );
              }
              break;
          }
      }

      configASSERT(counter);
  } while( ( xFailedSubscribeToTopic == true ) && ( counter ) );
}
/*-----------------------------------------------------------*/

void prvMQTTPublishToTopics( MQTTContext_t * pxMQTTContext ) {
  MQTTStatus_t xResult;
  MQTTPublishInfo_t xMQTTPublishInfo;
  uint32_t ulTopicCount;

  /***
   * For readability, error handling in this function is restricted to the use of
   * asserts().
   ***/

  for( ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++ )
  {
      /* Some fields are not used by this demo so start with everything at 0. */
      ( void ) memset( ( void * ) &xMQTTPublishInfo, 0x00, sizeof( xMQTTPublishInfo ) );

      /* This demo uses QoS2 */
      xMQTTPublishInfo.qos = MQTTQoS2;
      xMQTTPublishInfo.retain = false;
      xMQTTPublishInfo.pTopicName = xTopicFilterContext[ ulTopicCount ].pcTopicFilter;
      xMQTTPublishInfo.topicNameLength = ( uint16_t ) strlen( xTopicFilterContext[ ulTopicCount ].pcTopicFilter );
      xMQTTPublishInfo.pPayload = configMESSAGE;
      xMQTTPublishInfo.payloadLength = strlen( configMESSAGE );

      /* Get a unique packet id. */
      usPublishPacketIdentifier = MQTT_GetPacketId( pxMQTTContext );

      std::cout << "Publishing to the MQTT topic " << xTopicFilterContext[ ulTopicCount ].pcTopicFilter \
        << "." << std::endl;
      /* Send PUBLISH packet. */
      xResult = MQTT_Publish( pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier );
      configASSERT( xResult == MQTTSuccess );
  }
}
/*-----------------------------------------------------------*/

void prvMQTTUnsubscribeFromTopics( MQTTContext_t * pxMQTTContext )
{
    MQTTStatus_t xResult;
    MQTTSubscribeInfo_t xMQTTSubscription[ configTOPIC_COUNT ];
    uint32_t ulTopicCount;

    /* Some fields are not used by this demo so start with everything at 0. */
    memset( ( void * ) &xMQTTSubscription, 0x00, sizeof( xMQTTSubscription ) );

    /* Populate subscription list. */
    for(ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++)
    {
        xMQTTSubscription[ ulTopicCount ].qos = MQTTQoS2;
        xMQTTSubscription[ ulTopicCount ].pTopicFilter = xTopicFilterContext[ ulTopicCount ].pcTopicFilter;
        xMQTTSubscription[ ulTopicCount ].topicFilterLength = ( uint16_t ) strlen( xTopicFilterContext[ ulTopicCount ].pcTopicFilter );

        std::cout << "Unsubscribing from topic " << xTopicFilterContext[ ulTopicCount ].pcTopicFilter \
          << "." << std::endl;
    }

    /* Get next unique packet identifier. */
    usUnsubscribePacketIdentifier = MQTT_GetPacketId( pxMQTTContext );
    /* Make sure the packet id obtained is valid. */
    configASSERT( usUnsubscribePacketIdentifier != 0 );

    /* Send UNSUBSCRIBE packet. */
    xResult = MQTT_Unsubscribe( pxMQTTContext,
                                xMQTTSubscription,
                                sizeof( xMQTTSubscription ) / sizeof( MQTTSubscribeInfo_t ),
                                usUnsubscribePacketIdentifier );

    configASSERT( xResult == MQTTSuccess );
}
/*-----------------------------------------------------------*/

void prvMQTTProcessResponse( MQTTPacketInfo_t * pxIncomingPacket,
                                    uint16_t usPacketId )
{
    uint32_t ulTopicCount = 0U;

    switch( pxIncomingPacket->type )
    {
        case MQTT_PACKET_TYPE_PUBACK:
          std::cout << "PUBACK received for packet ID: " << usPacketId << std::endl;
            break;

        case MQTT_PACKET_TYPE_SUBACK:

            std::cout << "SUBACK received for packet ID: " <<  usPacketId << std::endl;

            /* A SUBACK from the broker, containing the server response to our subscription request, has been received.
             * It contains the status code indicating server approval/rejection for the subscription to the single topic
             * requested. The SUBACK will be parsed to obtain the status code, and this status code will be stored in global
             * variable #xTopicFilterContext. */
            prvUpdateSubAckStatus( pxIncomingPacket );

            for( ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++ )
            {
                if( xTopicFilterContext[ ulTopicCount ].xSubAckStatus != MQTTSubAckFailure )
                {
                  std::cout << "Subscribed to the topic " << xTopicFilterContext[ ulTopicCount ].pcTopicFilter \
                    << " with maximum QoS " <<  xTopicFilterContext[ ulTopicCount ].xSubAckStatus \
                    << "." << std::endl;
                }
            }

            /* Make sure ACK packet identifier matches with Request packet identifier. */
            configASSERT( usSubscribePacketIdentifier == usPacketId );
            break;

        case MQTT_PACKET_TYPE_UNSUBACK:
            std::cout << "UNSUBACK received for packet ID " << usPacketId << "." << std::endl;
            /* Make sure ACK packet identifier matches with Request packet identifier. */
            configASSERT( usUnsubscribePacketIdentifier == usPacketId );
            break;

        case MQTT_PACKET_TYPE_PINGRESP:

            /* Nothing to be done from application as library handles
             * PINGRESP with the use of MQTT_ProcessLoop API function. */
            std::cout << "PINGRESP should not be handled by the application " \
              << "callback when using MQTT_ProcessLoop." << std::endl;
            break;

        case MQTT_PACKET_TYPE_PUBREC:
            std::cout << "PUBREC received for packet id " << usPacketId << "." << std::endl;
            break;

        case MQTT_PACKET_TYPE_PUBREL:

            /* Nothing to be done from application as library handles
             * PUBREL. */
            std::cout << "PUBREL received for packet id " << usPacketId << "." << std::endl;
            break;

        case MQTT_PACKET_TYPE_PUBCOMP:

            /* Nothing to be done from application as library handles
             * PUBCOMP. */
            std::cout << "PUBCOMP received for packet id " << usPacketId << "." << std::endl;
            break;

        /* Any other packet type is invalid. */
        default:
            std::cout << "prvMQTTProcessResponse() called with unknown packet type: " \
              << pxIncomingPacket->type << "." << std::endl;
    }
}
/*-----------------------------------------------------------*/

void prvMQTTProcessIncomingPublish( MQTTPublishInfo_t * pxPublishInfo )
{
    uint32_t ulTopicCount;
    bool xTopicFound = false;

    configASSERT( pxPublishInfo != NULL );

    /* Process incoming Publish. */
    std::cout << "Incoming QoS: " << pxPublishInfo->qos << "." << std::endl;

    /* Verify the received publish is for one of the topics that's been subscribed to. */
    for( ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++)
    {
        if( ( pxPublishInfo->topicNameLength == strlen( xTopicFilterContext[ ulTopicCount ].pcTopicFilter ) ) &&
            ( strncmp( xTopicFilterContext[ ulTopicCount ].pcTopicFilter, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength ) == 0 ) )
        {
            xTopicFound = true;
            break;
        }
    }

    if( xTopicFound == true )
    {
      std::cout << "Incoming Publish Topic Name: " << pxPublishInfo->pTopicName \
        << " matches a subscribed topic." << std::endl;
    }
    else
    {
      std::cout << "Incoming Publish Topic Name: " << pxPublishInfo->pTopicName \
        << "does not match a subscribed topic." << std::endl;
    }

    /* Verify the message received matches the message sent. */
    if( strncmp( configMESSAGE, ( const char * ) ( pxPublishInfo->pPayload ), pxPublishInfo->payloadLength ) != 0 )
    {
      std::cout << "Incoming Publish Message does not match Expected Message." << std::endl;
    }
}

/*-----------------------------------------------------------*/

void prvEventCallback( MQTTContext_t * pxMQTTContext,
                              MQTTPacketInfo_t * pxPacketInfo,
                              MQTTDeserializedInfo_t * pxDeserializedInfo )
{
    /* The MQTT context is not used in this function. */
    ( void ) pxMQTTContext;

    if( ( pxPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        std::cout << "PUBLISH received for packet id " << pxDeserializedInfo->packetIdentifier << "." << std::endl;
        prvMQTTProcessIncomingPublish( pxDeserializedInfo->pPublishInfo );
    }
    else
    {
        prvMQTTProcessResponse( pxPacketInfo, pxDeserializedInfo->packetIdentifier );
    }
}

/*-----------------------------------------------------------*/

uint32_t prvGetTimeMs( void )
{
    uint32_t ulTimeMs = 0UL;
    std::chrono::duration<long int, std::ratio<1,1000>> dtn;

    /* Get the current time. */
    std::chrono::system_clock::time_point xTickCount = std::chrono::system_clock::now();

    //calculate time diff
    dtn = std::chrono::duration_cast<std::chrono::duration<long int, std::ratio<1,1000>>> (xTickCount - ulGlobalEntryTimeMs); //time diff in ms;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) dtn.count();

    return ulTimeMs;
}

/*-----------------------------------------------------------*/

MQTTStatus_t prvProcessLoopWithTimeout( MQTTContext_t * pMqttContext,
                                               uint32_t ulTimeoutMs )
{
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t eMqttStatus = MQTTSuccess;

    ulCurrentTime = pMqttContext->getTime();
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeoutMs;

    /* Call MQTT_ProcessLoop multiple times a timeout happens, or
     * MQTT_ProcessLoop fails. */
    while( ( ulCurrentTime < ulMqttProcessLoopTimeoutTime ) &&
           ( eMqttStatus == MQTTSuccess || eMqttStatus == MQTTNeedMoreBytes ) )
    {
        eMqttStatus = MQTT_ProcessLoop( pMqttContext );
        ulCurrentTime = pMqttContext->getTime();
    }

    if( eMqttStatus == MQTTNeedMoreBytes )
    {
        eMqttStatus = MQTTSuccess;
    }

    return eMqttStatus;
}

/*-----------------------------------------------------------*/

void prvInitializeTopicBuffers( void )
{
    uint32_t ulTopicCount;
    int xCharactersWritten;


    for(ulTopicCount = 0; ulTopicCount < configTOPIC_COUNT; ulTopicCount++)
    {
        /* Write topic strings into buffers. */
        xCharactersWritten = snprintf( xTopicFilterContext[ ulTopicCount ].pcTopicFilter,
                                       configTOPIC_BUFFER_SIZE,
                                       "%s%d", configTOPIC_PREFIX, ( int ) ulTopicCount );

        configASSERT( xCharactersWritten >= 0 && xCharactersWritten < configTOPIC_BUFFER_SIZE );

        /* Assign topic string to its corresponding SUBACK code initialized as a failure. */
        xTopicFilterContext[ ulTopicCount ].xSubAckStatus = MQTTSubAckFailure;
    }
}

/*-----------------------------------------------------------*/
