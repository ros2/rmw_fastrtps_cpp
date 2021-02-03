// Copyright 2019 Open Source Robotics Foundation, Inc.
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

#include <utility>
#include <string>

#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"

#include "rmw_fastrtps_shared_cpp/custom_participant_info.hpp"
#include "rmw_fastrtps_shared_cpp/custom_subscriber_info.hpp"
#include "rmw_fastrtps_shared_cpp/namespace_prefix.hpp"
#include "rmw_fastrtps_shared_cpp/qos.hpp"
#include "rmw_fastrtps_shared_cpp/rmw_common.hpp"
#include "rmw_fastrtps_shared_cpp/subscription.hpp"
#include "rmw_fastrtps_shared_cpp/TypeSupport.hpp"
#include "rmw_fastrtps_shared_cpp/utils.hpp"

namespace rmw_fastrtps_shared_cpp
{
rmw_ret_t
destroy_subscription(
  const char * identifier,
  CustomParticipantInfo * participant_info,
  rmw_subscription_t * subscription)
{
  assert(subscription->implementation_identifier == identifier);
  static_cast<void>(identifier);

  // Get RMW Subscriber
  rmw_ret_t ret = RMW_RET_OK;
  auto info = static_cast<CustomSubscriberInfo *>(subscription->data);

  // NOTE: Topic deletion and unregister type is done in the participant
  if (nullptr != info){
    // Delete DataReader
    ReturnCode_t ret = participant_info->subscriber_->delete_datareader(info->subscriber_);
    if (ret != ReturnCode_t::RETCODE_OK) {
      RMW_SET_ERROR_MSG("Fail in delete datareader");
      return rmw_fastrtps_shared_cpp::cast_error_dds_to_rmw(ret);
    }

    // Delete DataReader listener
    if (nullptr != info->listener_){
      delete info->listener_;
    }

    // Delete type support inside subscription
    if (info->type_support_ != nullptr) {
      delete info->type_support_;
    }

    // Delete SubscriberInfo structure
    delete info;
  }else{
    ret = RMW_RET_INVALID_ARGUMENT;
  }

  rmw_free(const_cast<char *>(subscription->topic_name));
  rmw_subscription_free(subscription);

  RCUTILS_CAN_RETURN_WITH_ERROR_OF(RMW_RET_ERROR);  // on completion
  return ret;
}
}  // namespace rmw_fastrtps_shared_cpp
