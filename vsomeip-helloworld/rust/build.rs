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
fn main() {
    // FIXME: use rust dependency to cmake install dir. 
    // For runtime it is OK, LD_LIBRARY_PATH should be set to include vsomeip_adapter.so / vsomeip*.so
    println!("cargo:rustc-link-search=native=../build/install/lib");
    println!("cargo:rustc-link-lib=dylib=vsomeip_adapter");
}