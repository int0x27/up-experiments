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
mod adapter_bindings;

use ctrlc;
use log::{debug, error, info};
use std::thread;
use std::ffi::CString;
use std::sync::Mutex;
use lazy_static::lazy_static;

// include generated adapter bindings
include!("adapter_bindings.rs");

// Define a wrapper struct for the vsomeip_adapter_context_t pointer, allowing to share it between threads
struct SafePtr(*const vsomeip_adapter_context_t);
unsafe impl Send for SafePtr {}
unsafe impl Sync for SafePtr {}


pub const HELLO_SERVICE_ID: vsomeip_service_t = 0x6000;
pub const HELLO_INSTANCE_ID: vsomeip_instance_t = 0x0001;
pub const HELLO_VER_MAJOR: vsomeip_major_version_t = 0x0001;
pub const HELLO_METHOD_ID: vsomeip_instance_t = 0x8001;
pub const HELLO_EVENTGROUP_ID: vsomeip_eventgroup_t = 0x0100;
pub const HELLO_EVENT_ID: vsomeip_method_t = 0x8005;

// Declare global vsomeip_config_t
static mut ADAPTER_CONFIG: Option<vsomeip_config_t> = None;

lazy_static! {
    static ref AVAILABLE_FLAG: Mutex<bool> = Mutex::new(false);
    static ref TERMINATE_FLAG: Mutex<bool> = Mutex::new(false);
}

static mut SHUTDOWN_FLAG: bool = false;
static mut HELLO_AVAILABLE_FLAG: bool = false;

// Define a function with the same signature as message_callback_t
unsafe extern "C" fn on_message_cb(
    service: vsomeip_service_t,
    instance: vsomeip_instance_t,
    method: vsomeip_method_t,
    payload: *const u8,
    payload_size: usize,
) -> i32 {

    // FIXME: might need vsomeip::message extra properties in the callback, e.g. get_message_type()

    // don't evaluate if debug is not enabled
    if log::log_enabled!(log::Level::Debug) {    
        // To print the payload, you need to create a slice from the raw pointer
        // Be careful with this, as it can lead to undefined behavior if the pointer is null or not valid
        let payload_str = if !payload.is_null() && payload_size > 0 {
            let slice = std::slice::from_raw_parts(payload, payload_size);
            format!("{:02x?}", slice)
        } else {
            String::from("<NULL>")
        };

        // Log all the information in a single log message
        debug!(
            "[on_message] Service [{:04x}:{:04x}.{:04x}] Payload({}) = {}",
            service, instance, method, payload_size, payload_str
        );
    }

    unsafe {
        if let Some(adapter_config) = &ADAPTER_CONFIG {
            let config = adapter_config;
            
            // Check for expected events
            if config.event_config.service == service && config.event_config.instance == instance && config.event_config.event == method {
                info!("Received Event from [{:04x}:{:04x}.{:04x}]", service, instance, method);
                // TODO: handle event
            }
            if config.service_config.service == service && config.service_config.instance == instance && config.service_config.method == method {
                info!("Received Message from [{:04x}:{:04x}.{:04x}]", service, instance, method);
                // TODO: handle response
            }
        }
    }

    0
}

unsafe extern "C" fn on_availability_cb(
    service: vsomeip_service_t,
    instance: vsomeip_instance_t,
    is_available: bool,
) -> i32 {
    info!(
        "[availability_callback] Service [{:04x}:{:04x}] is available: {}",
        service, instance, is_available
    );

    if service == HELLO_SERVICE_ID && instance == HELLO_INSTANCE_ID {
        // let mut flag = AVAILABLE_FLAG.lock().unwrap();
        // *flag = is_available;
        unsafe { HELLO_AVAILABLE_FLAG = is_available; }
    }

    0
}

fn main() {

    env_logger::init();
    
    info!("Starting vsomeip adapter...");
    
    // Create vsomeip adapter config (allow overriding from envronment variables)
    let app_name = CString::new(
        std::env::var("VSOMEIP_APPLICATION_NAME").unwrap_or("vsomeip_adapter".to_string())
    ).unwrap();
            
    let config_file = CString::new(
        std::env::var("VSOMEIP_CONFIGURATION").unwrap_or("vsomeip_adapter.json".to_string())
    ).unwrap();
    info!("Using app {:?}, configuration file: {:?}", app_name, config_file);

    std::env::set_var("VSOMEIP_APPLICATION_NAME", app_name.to_str().unwrap());
    std::env::set_var("VSOMEIP_CONFIGURATION", config_file.to_str().unwrap());

    unsafe {
        ADAPTER_CONFIG = Some(vsomeip_config_t {
            app_name: app_name.into_raw() as *mut _,
            config_file: config_file.into_raw() as *mut _,
            use_tcp: false,
            debug: 1,
            event_config: vsomeip_event_config_t {
                service: HELLO_SERVICE_ID,
                instance: HELLO_INSTANCE_ID,
                event_group: HELLO_EVENTGROUP_ID,
                event: HELLO_EVENT_ID,
                service_major: 1,
            },
            service_config: vsomeip_reqest_config_t {
                service: HELLO_SERVICE_ID,
                instance: HELLO_INSTANCE_ID,
                method: HELLO_METHOD_ID,
                service_major: 1,
                service_minor: 0,
            },
        });
    }

    let mut config = unsafe { ADAPTER_CONFIG.unwrap() };

    // let mut config = vsomeip_config_t {
    //     app_name: app_name.into_raw() as *mut _,
    //     config_file: config_file.into_raw() as *mut _,
    //     use_tcp: false,
    //     debug: 1,
    //     event_config: vsomeip_event_config_t {
    //         service: HELLO_SERVICE_ID,
    //         instance: HELLO_INSTANCE_ID,
    //         event_group: HELLO_EVENTGROUP_ID,
    //         event: HELLO_EVENT_ID,
    //         service_major: 1,
    //     },
    //     service_config: vsomeip_reqest_config_t {
    //         service: HELLO_SERVICE_ID,
    //         instance: HELLO_INSTANCE_ID,
    //         method: HELLO_METHOD_ID,
    //         service_major: 1,
    //         service_minor: 0,
    //     },
    // };

    unsafe { vsomeip_adapter_init_from_environment(&mut config as *mut vsomeip_config_t); }

    let ctx: *const vsomeip_adapter_context_t = unsafe { 
        create_vsomeip_adapter(
            &mut config as *mut vsomeip_config_t,
            Some(on_message_cb),
            Some(on_availability_cb)) 
    };

    let sig_context = SafePtr(ctx);

    ctrlc::set_handler(move || {
        info!("[SIGINT] Shutting down vsomeip adapter...");
        unsafe {
            if !SHUTDOWN_FLAG {
                debug!("[SIGINT] setting TERMINATE_FLAG");
                SHUTDOWN_FLAG = true;
                let th_ctx: *const vsomeip_adapter_context_t = sig_context.0;
                if th_ctx.is_null() {
                    error!("[SIGINT] vsomeip adapter ctx is NULL");
                } else {
                    info!("[SIGINT] Stopping vsomeip adapter ctx:{:p}", th_ctx);
                    let _rc:i32 = vsomeip_adapter_stop(th_ctx);
                    debug!("Waiting for threads to finish...");
                }
            } else {
                error!("[SIGINT] Already shutting down vsomeip adapter...");
            }
        }
    })
    .expect("Error setting SIGINT handler!");
    
    let rc: i32 = unsafe { vsomeip_adapter_init(ctx) };
    if rc != 0 {
        log::error!("Failed to initialize vsomeip adapter: {}", rc);
        std::process::exit(1);
    }

    let start_context = SafePtr(ctx);
    // vsomeip_adapter_start is blocking call handling vsomeip messaging
    let start_thread = thread::spawn(move || {
        unsafe { 
            let th_ctx: *const vsomeip_adapter_context_t = start_context.0;
            info!("Starting vsomeip adapter {:p} in new thread...", th_ctx);
            let _rc:i32 = vsomeip_adapter_start(th_ctx); 
        }
    });

    // send a message in different thread
    let req_context = SafePtr(ctx);
    let req_count: i32 = 1;

    // vsomeip_adapter_start is blocking call handling vsomeip messaging
    let req_thread = thread::spawn(move || {
        // Send service request
        let th_ctx: *const vsomeip_adapter_context_t = req_context.0;

        let service: vsomeip_service_t = HELLO_SERVICE_ID;
        let instance: vsomeip_instance_t = HELLO_INSTANCE_ID;
        let method: vsomeip_method_t = HELLO_EVENT_ID;

        let payload_str = "Rust World!\0";
        let payload = payload_str.as_bytes();
        let payload_size: usize = std::mem::size_of_val(&payload); // Initialize payload size
        
        // needs time for vsomeip
        
        loop {
            unsafe {
                if SHUTDOWN_FLAG {
                    debug!("[req_thread] terminated on shuttdown!");
                    return;
                }
                if HELLO_AVAILABLE_FLAG {
                    debug!("[req_thread] Service is available!");
                    break;
                }
            }
            //let flag = AVAILABLE_FLAG.lock().unwrap();
            //if *flag {}

            log::trace!("[req_thread] Service is unavailable..");
            thread::sleep(std::time::Duration::from_millis(500));
        }
        thread::sleep(std::time::Duration::from_millis(1000));
        for i in 0..req_count {
            info!("- Sending service request #{}...", i);
            let result = unsafe {
                vsomeip_adapter_send_service_request(
                    th_ctx,
                    service,
                    instance,
                    method,
                    payload_str.as_ptr(),
                    payload_size,
                )
            };
            info!("Send service request result: {}", result);
            thread::sleep(std::time::Duration::from_millis(100));
        }
    });

    debug!("Waiting for request thread to finish...");
    req_thread.join().unwrap();

    debug!("Waiting for adapter thread to finish...");
    start_thread.join().unwrap();

    // Destroy vsomeip adapter
    unsafe { destroy_vsomeip_adapter(ctx as *mut vsomeip_adapter_context_t) };
    // WARNING! ctx is invalid after this point.

}
