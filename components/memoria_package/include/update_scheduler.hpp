/**
 * @file update_scheduler.hpp
 */
#pragma once

namespace memoria {
namespace package {
/* 启动后台调度任务（必须在 kernel init 之后调一次） */
void start_scheduler();
int  run_nightly_update();
} }
