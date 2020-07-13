// Copyright 2019 Open Source Robotics Foundation, Inc.
// Copyright 2016-2018 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <mutex>
#include <utility>
#include <set>
#include <string>

#include "rcutils/logging_macros.h"
#include "rcutils/strdup.h"

#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/impl/cpp/macros.hpp"
#include "rmw/rmw.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

#include "rcpputils/scope_exit.hpp"

#include "rmw_dds_common/context.hpp"

#include "rmw_fastrtps_shared_cpp/custom_participant_info.hpp"
#include "rmw_fastrtps_shared_cpp/rmw_common.hpp"
#include "rmw_fastrtps_shared_cpp/rmw_context_impl.hpp"


namespace rmw_fastrtps_shared_cpp
{
rmw_node_t *
__rmw_create_node(
  rmw_context_t * context,
  const char * identifier,
  const char * name,
  const char * namespace_)
{
  assert(nullptr != context);
  assert(nullptr != context->impl);
  assert(identifier == context->implementation_identifier);
  static_cast<void>(identifier);

  int validation_result = RMW_NODE_NAME_VALID;
  rmw_ret_t ret = rmw_validate_node_name(name, &validation_result, nullptr);
  if (RMW_RET_OK != ret || RMW_NODE_NAME_VALID != validation_result) {
    const char * reason = RMW_RET_OK == ret ?
      rmw_node_name_validation_result_string(validation_result) :
      rmw_get_error_string().str;
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("invalid node name: %s", reason);
    return nullptr;
  }
  validation_result = RMW_NAMESPACE_VALID;
  ret = rmw_validate_namespace(namespace_, &validation_result, nullptr);
  if (RMW_RET_OK != ret || RMW_NAMESPACE_VALID != validation_result) {
    const char * reason = RMW_RET_OK == ret ?
      rmw_node_name_validation_result_string(validation_result) :
      rmw_get_error_string().str;
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("invalid node namespace: %s", reason);
    return nullptr;
  }

  auto common_context = static_cast<rmw_dds_common::Context *>(context->impl->common);
  rmw_dds_common::GraphCache & graph_cache = common_context->graph_cache;
  rcutils_allocator_t allocator = context->options.allocator;
  rmw_node_t * node = reinterpret_cast<rmw_node_t *>(
    allocator.zero_allocate(1u, sizeof(rmw_node_t), allocator.state));
  if (nullptr == node) {
    RMW_SET_ERROR_MSG("failed to allocate node");
    return nullptr;
  }
  auto cleanup_node = rcpputils::make_scope_exit(
    [node, allocator]() {
      allocator.deallocate(const_cast<char *>(node->name), allocator.state);
      allocator.deallocate(const_cast<char *>(node->namespace_), allocator.state);
      allocator.deallocate(node, allocator.state);
    });
  node->implementation_identifier = context->implementation_identifier;
  node->data = nullptr;

  node->name = rcutils_strdup(name, allocator);
  if (nullptr == node->name) {
    RMW_SET_ERROR_MSG("failed to copy node name");
    return nullptr;
  }

  node->namespace_ = rcutils_strdup(namespace_, allocator);
  if (nullptr == node->namespace_) {
    RMW_SET_ERROR_MSG("failed to copy node namespace");
    return nullptr;
  }
  node->context = context;

  {
    // Though graph_cache methods are thread safe, both cache update and publishing have to also
    // be atomic.
    // If not, the following race condition is possible:
    // node1-update-get-message / node2-update-get-message / node2-publish / node1-publish
    // In that case, the last message published is not accurate.
    std::lock_guard<std::mutex> guard(common_context->node_update_mutex);
    rmw_dds_common::msg::ParticipantEntitiesInfo participant_msg =
      graph_cache.add_node(common_context->gid, name, namespace_);
    if (RMW_RET_OK != __rmw_publish(
        node->implementation_identifier,
        common_context->pub,
        static_cast<void *>(&participant_msg),
        nullptr))
    {
      return nullptr;
    }
  }
  cleanup_node.cancel();
  return node;
}

rmw_ret_t
__rmw_destroy_node(
  const char * identifier,
  rmw_node_t * node)
{
  assert(node != nullptr);
  assert(node->implementation_identifier == identifier);
  static_cast<void>(identifier);

  assert(node->context != nullptr);
  assert(node->context->impl != nullptr);
  assert(node->context->impl->common != nullptr);
  auto common_context = static_cast<rmw_dds_common::Context *>(node->context->impl->common);
  rmw_dds_common::GraphCache & graph_cache = common_context->graph_cache;
  {
    std::lock_guard<std::mutex> guard(common_context->node_update_mutex);
    rmw_dds_common::msg::ParticipantEntitiesInfo participant_msg =
      graph_cache.remove_node(common_context->gid, node->name, node->namespace_);
    rmw_ret_t ret = __rmw_publish(
      node->implementation_identifier,
      common_context->pub,
      static_cast<void *>(&participant_msg),
      nullptr);
    if (RMW_RET_OK != ret) {
      return ret;
    }
  }
  rcutils_allocator_t allocator = node->context->options.allocator;
  allocator.deallocate(const_cast<char *>(node->name), allocator.state);
  allocator.deallocate(const_cast<char *>(node->namespace_), allocator.state);
  allocator.deallocate(node, allocator.state);
  return RMW_RET_OK;
}

const rmw_guard_condition_t *
__rmw_node_get_graph_guard_condition(const rmw_node_t * node)
{
  auto common_context = static_cast<rmw_dds_common::Context *>(node->context->impl->common);
  if (!common_context) {
    RMW_SET_ERROR_MSG("common_context is nullptr");
    return nullptr;
  }
  return common_context->graph_guard_condition;
}
}  // namespace rmw_fastrtps_shared_cpp
