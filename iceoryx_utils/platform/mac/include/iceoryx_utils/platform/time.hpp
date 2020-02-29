// Copyright (c) 2020 by Robert Bosch GmbH. All rights reserved.
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

#pragma once

#include <sys/time.h>

using timer_t = void*;

struct itimerspec
{
    timespec it_interval;
    timespec it_value;
};

#if 0
#include <dispatch/dispatch.h>

#include <chrono>
#include <iostream>
#include <thread>

int test(dispatch_source_t sourceTimer) { printf("test called\n"); }

int main() {
    dispatch_queue_attr_t queueAttributes = 0;
    printf("creating queue\n");
    dispatch_queue_t timerQueue =
        dispatch_queue_create("timerQueueName", queueAttributes);

    uintptr_t timerHandle;
    unsigned long timerMask;
    printf("creating timer\n");
    dispatch_source_t timer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, timerHandle, timerMask, timerQueue);

    printf("set handler\n");
    dispatch_source_set_event_handler(timer, ^{
      test(timer);
    });

    printf("set cancel handler\n");
    dispatch_source_set_cancel_handler(timer, ^{
      dispatch_release(timer);
      dispatch_release(timerQueue);
      printf("cleaned up timer and timerQueue");
    });

    printf("set timer interval\n");
    dispatch_time_t start = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 10);
    dispatch_source_set_timer(timer, start, 1 * NSEC_PER_SEC / 10, 0);

    printf("activate timer\n");
    dispatch_resume(timer);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    printf("cleaning up timer\n");
    dispatch_source_cancel(timer);
}

#endif

inline int timer_create(clockid_t clockid, struct sigevent* sevp, timer_t* timerid)
{
    // dispatch_queue_t timerQueue =
    //    dispatch_queue_create("timerQueueName", queueAttributes);
    // dispatch_source_t timer = dispatch_source_create(
    //    DISPATCH_SOURCE_TYPE_TIMER, timerHandle, timerMask, timerQueue);
    // dispatch_source_set_cancel_handler(timer, ^{
    //  dispatch_release(timer);
    //  dispatch_release(timerQueue);
    //  printf("cleaned up timer and timerQueue");
    //});
    // dispatch_resume(timer);
    return 0;
}

inline int timer_delete(timer_t timerid)
{
    // dispatch_source_cancel(timer);
    return 0;
}

inline int timer_settime(timer_t timerid, int flags, const struct itimerspec* new_value, struct itimerspec* old_value)
{
    // dispatch_source_set_timer(timer, start, 1 * NSEC_PER_SEC / 10, 0);
    return 0;
}

inline int timer_gettime(timer_t timerid, struct itimerspec* curr_value)
{
    return 0;
}

inline int timer_getoverrun(timer_t timerid)
{
    return 0;
}
