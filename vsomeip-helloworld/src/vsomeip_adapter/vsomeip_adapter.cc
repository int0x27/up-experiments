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
#include <stdio.h>

#include <condition_variable>
#include <memory>
#include <mutex>

#include "vsomeip/vsomeip.hpp"

#include "vsomeip_adapter.h"

class vsomeip_adapter {
private:
    std::shared_ptr<vsomeip::application> app_;
    bool use_tcp_;      // TCP or UDP endpoint config
    bool is_registered_; // is service registered
    bool is_available_; // is requested service available
    bool running_;      // is adapter running

    std::mutex mutex_;
    std::condition_variable condition_;
    
    vsomeip_config_t* config_;
    message_callback_t message_cb_;
    availability_callback_t avail_cb_;

public:
    vsomeip_adapter(vsomeip_config_t* config, message_callback_t message_cb, availability_callback_t avail_cb) :
        app_(vsomeip::runtime::get()->create_application(config->app_name))
        , use_tcp_(config && config->use_tcp) // NOTE: config_ not initialized yet
        , is_registered_(false)
        , is_available_(false)
        , running_(false)
        , config_(config)
        , message_cb_(message_cb)
        , avail_cb_(avail_cb)
    {
    }

    ~vsomeip_adapter() {
        stop();
    }

    int32_t init() {
        std::lock_guard<std::mutex> its_lock(mutex_);
        if (config_->debug > 0) printf("[vsomeip_adapter::init] called.\n");
        if (!app_->init()) {
            printf("[vsomeip_adapter::init] Failed to initialize vsomeip application!\n");
            return -1;
        }

        printf("### vsomeip_adapter settings [cli_id=0x%x, app='%s', protocol=%s, routing=%d]\n",
            app_->get_client(), app_->get_name().c_str(), (use_tcp_ ? "TCP" : "UDP"), 
            app_->is_routing());
                        
        // TODO: integrate app_->set_watchdog_handler()
        app_->register_state_handler(
                std::bind(&vsomeip_adapter::on_state, this,
                        std::placeholders::_1));

        app_->register_message_handler(
                vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                std::bind(&vsomeip_adapter::on_message, this,
                        std::placeholders::_1));

        app_->register_availability_handler(
                vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE,
                std::bind(&vsomeip_adapter::on_availability, this,
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                vsomeip::ANY_MAJOR, vsomeip::ANY_MINOR);

        app_->register_routing_state_handler(
            std::bind(&vsomeip_adapter::on_routing_state_changed, this,
                        std::placeholders::_1));
        return 0;
    }

    void start() {
        if (config_->debug > 0) printf("[vsomeip_adapter::start] starting vsomeip app....\n");
        app_->start();
    }
    
    int stop() {
        if (config_->debug > 0) printf("[vsomeip_adapter::stop] called.\n");
        
        // unregister the state handler
        if (config_->debug > 1) printf("[vsomeip_adapter::stop] unregister_state_handler()\n");
        app_->unregister_state_handler();
        if (config_->debug > 1) printf("[vsomeip_adapter::stop] unregister_availability_handler()\n");        
        app_->unregister_availability_handler(
            vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE);
        // unregister the message handler
        if (config_->debug > 1) printf("[vsomeip_adapter::stop] unregister_message_handler()\n");        
        app_->unregister_message_handler(
                vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD);
        app_->clear_all_handler();
        // shutdown the application
        if (config_->debug > 1) printf("[vsomeip_adapter::stop] app->stop()\n");        
        app_->stop();
        return 0;
    }

    int send_service_request(
        vsomeip_service_t service,
        vsomeip_instance_t instance,
        vsomeip_method_t method,
        const uint8_t* payload,
        size_t payload_size) 
    {
        if (config_->debug > 0) printf("[vsomeip_adapter::send_service_request] called.\n");
        std::shared_ptr<vsomeip::message> rq = vsomeip::runtime::get()->create_request(use_tcp_);
        rq->set_service(service);
        rq->set_instance(instance);
        rq->set_method(method);
        std::shared_ptr<vsomeip::payload> pl = vsomeip::runtime::get()->create_payload();
        pl->set_data(payload, payload_size);
        rq->set_payload(pl);
        app_->send(rq);
        return 0;
    }

protected:

    void on_state(vsomeip::state_type_e _state) {
        if (_state == vsomeip::state_type_e::ST_REGISTERED) {
            is_registered_ = true;
            if (config_->debug > 0) printf("[on_state] ST_REGISTERED.\n");
            
            // TODO: handle multiple services, e.g. config_->services[]
            printf("[on_state] ## Requesting Service [%04x.%04x v%d.%d]\n", 
                config_->service_config.service, 
                config_->service_config.instance,
                config_->service_config.service_major,
                config_->service_config.service_minor);

            app_->request_service(
                config_->service_config.service, 
                config_->service_config.instance,
                config_->service_config.service_major,
                config_->service_config.service_minor);
        } else {
            if (config_->debug > 0) printf("[on_state] ST_DEREGISTERED.\n");
        }
    }


    void request_event(vsomeip::service_t _service, vsomeip::instance_t _instance, vsomeip::eventgroup_t _group, vsomeip::event_t _event, vsomeip::major_version_t _major) {
        printf("## Requesting Event [%04x.%04x/%04x]\n", _service, _instance, _event);
        std::set<vsomeip::eventgroup_t> its_groups;
        its_groups.insert(_group);
        app_->request_event(
                _service, _instance, _event,
                its_groups, vsomeip::event_type_e::ET_FIELD,
                (use_tcp_ ? vsomeip::reliability_type_e::RT_RELIABLE : vsomeip::reliability_type_e::RT_UNRELIABLE));

        printf("## Subscribe(%04x.%04x/%04x v%d)\n", _service, _instance, _group, _major);
        app_->subscribe(_service, _instance, _group, _major);
    }

    void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available) {
        // TODO: handle multiple services, e.g. config_->services[] or just forward to availabilty cb
        if (_service != config_->service_config.service ||
             _instance != config_->service_config.instance) {
            printf("### Unknown Service [%04x:%04x] is %s\n",
                _service, _instance, _is_available ? "Available." : "NOT available.");
            return;
        }
        printf("### SOME/IP Service [%04x:%04x] is %s\n",
            _service, _instance, _is_available ? "Available." : "NOT available.");
        {
            std::lock_guard<std::mutex> its_lock(mutex_);
            is_available_ = _is_available;
            if (config_->debug > 1) printf("// [on_availability] notify is_available_=%s\n", is_available_ ? "true" : "false");
            condition_.notify_one();
        }
        avail_cb_(_service, _instance, _is_available);
        
        // register events
        if (config_->event_config.service == _service && config_->event_config.instance == _instance) {
            request_event(
                config_->event_config.service, config_->event_config.instance,
                config_->event_config.event_group, config_->event_config.event,
                config_->event_config.service_major);
        }
    }

    void on_routing_state_changed(vsomeip::routing_state_e state) {
        printf("[on_routing_state_changed] routing_state_e:%d\n", (int)state);
    }

    void on_message(const std::shared_ptr<vsomeip::message>& _response) {
        if (message_cb_) {
            if (config_->debug > 2) printf("[on_message] calling message callback\n");
            message_cb_(
                _response->get_service(),
                _response->get_instance(),
                _response->get_method(),
                static_cast<const uint8_t*>(_response->get_payload()->get_data()),
                _response->get_length());
        }
    }

};

#ifdef __cplusplus
extern "C" {
#endif

vsomeip_adapter_context_t* create_vsomeip_adapter(
            vsomeip_config_t* config,
            message_callback_t message_cb,
            availability_callback_t availability_cb) 
{
    printf("[create_vsomeip_adapter] config:%p, msg_cb:%p, avail_cb:%p\n", config, message_cb, availability_cb);

    // TODO: Implement this function
    vsomeip_adapter_context_t *ctx = new vsomeip_adapter_context_t;
    if (!ctx) {
        printf("[create_vsomeip_adapter] no memory!\n");
        return nullptr;
    }
    ctx->context = new vsomeip_adapter(config, message_cb, availability_cb);
    if (config->debug) printf("[create_vsomeip_adapter] --> ctx:%p, ctx->context:%p\n", ctx, ctx->context);
    return ctx;
}

void destroy_vsomeip_adapter(vsomeip_adapter_context_t* ctx) {
    // TODO: Implement this function
    printf("[destroy_vsomeip_adapter] ctx:%p\n", ctx);
    // FIXME: sync, checks for npe
    if (!ctx) return;
    vsomeip_adapter* adapter = static_cast<vsomeip_adapter*>(ctx->context);
    if (adapter) {
        delete adapter;
        ctx->context = nullptr;
    }
    delete ctx;
}

int32_t vsomeip_adapter_init(const vsomeip_adapter_context_t* ctx) {
    printf("[vsomeip_adapter_init] ctx:%p\n", ctx);
    if (!ctx || !ctx->context) {
        printf("[vsomeip_adapter_init] Invalid context!\n");
        return -1;
    }
    // TODO: Implement this function
    vsomeip_adapter* adapter = static_cast<vsomeip_adapter*>(ctx->context);
    int32_t rc = adapter->init();
    return rc;
}

void vsomeip_adapter_init_from_environment(vsomeip_config_t* config) {
    printf("[vsomeip_adapter_init_from_environment] config:%p\n", config);
    // TODO: Implement this function
    if (!config) {
        printf("[vsomeip_adapter_init_from_environment] Invalid config!\n");
        return; // FIXME
    }
    if (getenv("VA_DEBUG")) config->debug = atoi(getenv("VA_DEBUG"));
    
    printf("[vsomeip_adapter_init_from_environment] config { name:\"%s\", conf:%s, debug:%d, use_tcp:%d"
           ", service_cfg { [%04x:%04x] v:%d.%d }"
           ", event_cfg: { [%04x:%04x] (%04x/%04x) v:%d } }\n",
        config->app_name, config->config_file, config->debug, config->use_tcp,
        
        config->service_config.service, config->service_config.instance,
        config->service_config.service_major, config->service_config.service_minor,
        
        config->event_config.service, config->event_config.instance,
        config->event_config.event_group, config->event_config.event, config->event_config.service_major);
 }

int32_t vsomeip_adapter_send_service_request(
    const vsomeip_adapter_context_t* ctx,
    vsomeip_service_t service,
    vsomeip_instance_t instance,
    vsomeip_method_t method,
    const uint8_t* payload,
    size_t payload_size
) {
    if (!ctx || !ctx->context) {
        printf("[vsomeip_adapter_send_service_request] Invalid context!\n");
        return -1;
    }

    printf("[vsomeip_adapter_send_service_request] ctx:%p, service:%04x, instance:%04x, method:%04x, payload:%p, payload_size:%zu\n",
            ctx, service, instance, method, payload, payload_size);
    // TODO: Implement this function
    vsomeip_adapter* adapter = static_cast<vsomeip_adapter*>(ctx->context);
    
    int32_t rc = adapter->send_service_request(service, instance, method, payload, payload_size);
    return rc;
}

int32_t vsomeip_adapter_start(const vsomeip_adapter_context_t* ctx) {
    printf("[vsomeip_adapter_start] ctx:%p\n", ctx);
    if (!ctx || !ctx->context) {
        printf("[vsomeip_adapter_start] Invalid context!\n");
        return -1;
    }    
    // TODO: Implement this function
    vsomeip_adapter* adapter = static_cast<vsomeip_adapter*>(ctx->context);
    adapter->start();
    return 0;
}

int32_t vsomeip_adapter_stop(const vsomeip_adapter_context_t* ctx) {
    printf("[vsomeip_adapter_stop] ctx:%p\n", ctx);
    if (!ctx || !ctx->context) {
        printf("[vsomeip_adapter_stop] Invalid context!\n");
        return -1;
    }
    // TODO: Implement this function
    vsomeip_adapter* adapter = static_cast<vsomeip_adapter*>(ctx->context);
    int32_t rc = adapter->stop();
    return rc;
}


#ifdef __cplusplus
} // extern  "C"
#endif