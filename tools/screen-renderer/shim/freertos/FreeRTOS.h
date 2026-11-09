// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for FreeRTOS: just its handle types, for SensorService.h's
/// members.
///
/// They're plain `void*` here: the renderer never creates a task, queue or
/// semaphore.
#pragma once

typedef void* QueueHandle_t;      ///< A queue.
typedef void* SemaphoreHandle_t;  ///< A semaphore or mutex.
typedef void* TaskHandle_t;       ///< A task.
