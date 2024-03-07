/*
 * Copyright (c) 2024 Contributors to the Eclipse Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef __cplusplus
extern "C" {
#endif

/*

 bindgen --blocklist-item "__.*" --blocklist-function "pthread_.*" --blocklist-type "pthread_.*" src/vsomeip_adapter/vsomeip_adapter.h \
    --output rust/src/adapter_bindings.rs -- -I /usr/include/c++/11 -I /usr/include/x86_64-linux-gnu/c++/11

 NOTE: Use 'int32_t' for the interface as 'int' is bound to ::std::os::raw::c_int in rust
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// #include <vsomeip/vsomeip.hpp>

typedef uint16_t    vsomeip_service_t;
typedef uint16_t    vsomeip_instance_t;
typedef uint16_t    vsomeip_method_t;
typedef uint16_t    vsomeip_eventgroup_t;
typedef uint16_t    vsomeip_event_t;

typedef uint8_t     vsomeip_major_version_t;
typedef uint32_t    vsomeip_minor_version_t;

typedef uint32_t    vsomeip_message_t;
typedef uint16_t    vsomeip_client_t;
typedef uint16_t    vsomeip_session_t;

// someip sample values
const vsomeip_service_t SAMPLE_SERVICE_ID = 0x1234;
const vsomeip_instance_t SAMPLE_INSTANCE_ID = 0x5678;
const vsomeip_instance_t SAMPLE_METHOD_ID = 0x7001;
const vsomeip_method_t SAMPLE_EVENT_ID = 0x0001;
const vsomeip_eventgroup_t SAMPLE_EVENTGROUP_ID = 0x0100;


/**
 * @brief vsomeip client request/response config
 */
typedef struct {
   
    /**
     * @brief SOME/IP Service ID for request/response
     */
    vsomeip_service_t service;

    /**
     * @brief SOME/IP Instance ID for request/response
     */
    vsomeip_instance_t instance;

    /**
     * @brief SOME/IP Method ID or request/response
     */
    vsomeip_method_t method;

    /**
     * @brief SOME/IP Service major version. May be needed if service/someip
     * impl register with major != 0
     */
    vsomeip_major_version_t service_major;

    /**
     * @brief SOME/IP Service minor version
     */
    vsomeip_minor_version_t service_minor;

} vsomeip_reqest_config_t;


/**
 * @brief vsomeip client event config
 */
typedef struct {
   
    /**
     * @brief SOME/IP Service ID to subscribe
     */
    vsomeip_service_t service;

    /**
     * @brief SOME/IP Instance ID to subscribe
     */
    vsomeip_instance_t instance;

    /**
     * @brief SOME/IP EventGroup ID
     */
    vsomeip_eventgroup_t event_group;

    /**
     * @brief SOME/IP Event ID
     */
    vsomeip_event_t event;

    /**
     * @brief SOME/IP Service major version. May be needed if service/someip
     * impl register with major != 0
     */
    vsomeip_major_version_t service_major;

} vsomeip_event_config_t;

/**
 * @brief SOME/IP Client configuration.
 *
 * NOTE: There is a dependency on app_name and config file specified in
 * VSOMEIP_CONFIGURATION environment variable.
 */
typedef struct {

    /**
     * @brief vsomeip Application Name. Must match provided app_config json file!
     * Also defined in "VSOMEIP_APPLICATION_NAME" environment variable.
     */
    char* app_name;

    /**
     * @brief Just a reference to exported "VSOMEIP_CONFIGURATION" environment variable
     */
    char* config_file;

    /**
     * @brief If true, reliable endpoits should be used, depends on the notify server configuration
     */
    bool use_tcp;

    /**
     * @brief someip client debug verbosity (0=quiet, ...)
     */
    int32_t debug;

    vsomeip_event_config_t event_config;

    vsomeip_reqest_config_t service_config;
    
} vsomeip_config_t;


/**
 * @brief callback std::function for handling incoming SOME/IP payload
 *
 * @param payload uint8_t* someip notification payload
 * @param size someip payload size
 * @return <0 on error
 */
typedef int32_t (*message_callback_t)(vsomeip_service_t service,
                                  vsomeip_instance_t instance,
                                  vsomeip_method_t event,
                                  const uint8_t *payload,
                                  size_t payload_size);

typedef int32_t (*availability_callback_t)(vsomeip_service_t service,
                                       vsomeip_instance_t instance,
                                       bool is_available);

/**
 * @brief vsomeip adapter context
 */
typedef struct {
    void *context;
} vsomeip_adapter_context_t;

void vsomeip_adapter_init_from_environment(
            vsomeip_config_t* config);

vsomeip_adapter_context_t* create_vsomeip_adapter(
            vsomeip_config_t* config,
            message_callback_t message_cb,
            availability_callback_t availability_cb);

void destroy_vsomeip_adapter(
            vsomeip_adapter_context_t* ctx);

int32_t vsomeip_adapter_send_service_request(
            const vsomeip_adapter_context_t* ctx, 
            vsomeip_service_t service,
            vsomeip_instance_t instance,
            vsomeip_method_t method,
            const uint8_t *payload,
            size_t payload_size);


int32_t vsomeip_adapter_init(const vsomeip_adapter_context_t* ctx);
int32_t vsomeip_adapter_start(const vsomeip_adapter_context_t* ctx);
int32_t vsomeip_adapter_stop(const vsomeip_adapter_context_t* ctx);

#ifdef __cplusplus
} // extern  "C"
#endif
