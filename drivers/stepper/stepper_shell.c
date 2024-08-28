/*
 * Copyright (c) 2024, Fabian Blatz <fabianblatz@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/shell/shell.h>
#include <zephyr/device.h>
#include <zephyr/drivers/stepper.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stepper_shell, CONFIG_STEPPER_LOG_LEVEL);

enum {
	arg_idx_dev = 1,
	arg_idx_param = 2,
	arg_idx_value = 3,
};

#ifdef CONFIG_STEPPER_SHELL_ASYNC

static struct k_poll_signal stepper_signal;
static struct k_poll_event stepper_poll_event =
	K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &stepper_signal);

static bool poll_thread_started;
K_THREAD_STACK_DEFINE(poll_thread_stack, CONFIG_STEPPER_SHELL_THREAD_STACK_SIZE);
static struct k_thread poll_thread;
static int start_polling(const struct shell *sh);

#endif /* CONFIG_STEPPER_SHELL_ASYNC */

static int parse_device_arg(const struct shell *sh, char **argv, const struct device **dev)
{
	*dev = device_get_binding(argv[arg_idx_dev]);
	if (!*dev) {
		shell_error(sh, "Stepper device %s not found", argv[arg_idx_dev]);
		return -ENODEV;
	}
	return 0;
}

static int cmd_stepper_enable(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	bool enable;

	if (strcmp(argv[arg_idx_param], "on") == 0) {
		enable = true;
	} else if (strcmp(argv[arg_idx_param], "off") == 0) {
		enable = false;
	} else {
		shell_error(sh, "Invalid enable value: %s", argv[arg_idx_param]);
		return -EINVAL;
	}

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	err = stepper_enable(dev, enable);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_move(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	int32_t micro_steps = strtol(argv[arg_idx_param], NULL, 0);
	struct k_poll_signal *poll_signal =
		COND_CODE_1(CONFIG_STEPPER_SHELL_ASYNC, (&stepper_signal), (NULL));

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	if (IS_ENABLED(CONFIG_STEPPER_SHELL_ASYNC)) {
		start_polling(sh);
	}

	err = stepper_move(dev, micro_steps, poll_signal);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_set_max_velocity(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	uint32_t velocity = strtoul(argv[arg_idx_param], NULL, 0);

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	err = stepper_set_max_velocity(dev, velocity);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_set_micro_step_res(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	enum micro_step_resolution resolution = atoi(argv[arg_idx_param]);

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	err = stepper_set_micro_step_res(dev, resolution);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_set_actual_position(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	int32_t position = strtol(argv[arg_idx_param], NULL, 0);

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	err = stepper_set_actual_position(dev, position);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_set_target_position(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	int32_t position = strtol(argv[arg_idx_param], NULL, 0);
	struct k_poll_signal *poll_signal =
		COND_CODE_1(CONFIG_STEPPER_SHELL_ASYNC, (&stepper_signal), (NULL));

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	if (IS_ENABLED(CONFIG_STEPPER_SHELL_ASYNC)) {
		start_polling(sh);
	}

	err = stepper_set_target_position(dev, position, poll_signal);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_enable_constant_velocity_mode(const struct shell *sh, size_t argc,
						     char **argv)
{
	const struct device *dev;
	int err;
	enum stepper_direction direction = atoi(argv[arg_idx_param]);
	uint32_t velocity = strtoul(argv[arg_idx_value], NULL, 0);

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	err = stepper_enable_constant_velocity_mode(dev, direction, velocity);
	if (err) {
		shell_error(sh, "Error: %d", err);
	}

	return err;
}

static int cmd_stepper_info(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *dev;
	int err;
	bool is_moving = false;
	int32_t actual_position = 0;
	enum micro_step_resolution micro_step_res = 0;

	err = parse_device_arg(sh, argv, &dev);
	if (err < 0) {
		return err;
	}

	shell_print(sh, "Stepper Info:");
	shell_print(sh, "Device: %s", dev->name);

	err = stepper_get_actual_position(dev, &actual_position);
	if (err < 0) {
		shell_warn(sh, "Failed to get actual position: %d", err);
	} else {
		shell_print(sh, "Actual Position: %d", actual_position);
	}

	err = stepper_get_micro_step_res(dev, &micro_step_res);
	if (err < 0) {
		shell_warn(sh, "Failed to get micro-step resolution: %d", err);
	} else {
		shell_print(sh, "Micro-step Resolution: %d", micro_step_res);
	}

	err = stepper_is_moving(dev, &is_moving);
	if (err < 0) {
		shell_warn(sh, "Failed to check if the motor is moving: %d", err);
	} else {
		shell_print(sh, "Is Moving: %s", is_moving ? "Yes" : "No");
	}

	return 0;
}

static void cmd_pos_stepper_motor_name(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_lookup(idx, NULL);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = "List Devices";
	entry->subcmd = NULL;
}

#ifdef CONFIG_STEPPER_SHELL_ASYNC

void stepper_poll_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	const struct shell *sh = p1;

	while (1) {
		k_poll(&stepper_poll_event, 1, K_FOREVER);

		if (stepper_poll_event.signal->result == STEPPER_SIGNAL_STEPS_COMPLETED) {
			shell_print(sh, "Stepper: All steps completed");
			k_poll_signal_reset(&stepper_signal);
		}
	}
}

static int start_polling(const struct shell *sh)
{
	if (!poll_thread_started) {
		k_tid_t tid;

		k_poll_signal_init(&stepper_signal);
		tid = k_thread_create(&poll_thread, poll_thread_stack,
				      CONFIG_STEPPER_SHELL_THREAD_STACK_SIZE, stepper_poll_thread,
				      (void *)sh, NULL, NULL, CONFIG_STEPPER_SHELL_THREAD_PRIORITY,
				      0, K_NO_WAIT);
		if (!tid) {
			shell_error(sh, "Cannot start poll thread");
			return -ENOEXEC;
		}

		k_thread_name_set(tid, "stepper_poll_thread");
		k_thread_start(tid);
		poll_thread_started = true;
	}
	return 0;
}

#endif /* CONFIG_STEPPER_SHELL_ASYNC */

SHELL_DYNAMIC_CMD_CREATE(dsub_pos_stepper_motor_name, cmd_pos_stepper_motor_name);

SHELL_STATIC_SUBCMD_SET_CREATE(
	stepper_cmds,
	SHELL_CMD_ARG(enable, &dsub_pos_stepper_motor_name, "<device> <on/off>", cmd_stepper_enable,
		      3, 0),
	SHELL_CMD_ARG(move, &dsub_pos_stepper_motor_name, "<device> <micro_steps>",
		      cmd_stepper_move, 3, 0),
	SHELL_CMD_ARG(set_max_velocity, &dsub_pos_stepper_motor_name, "<device> <velocity>",
		      cmd_stepper_set_max_velocity, 3, 0),
	SHELL_CMD_ARG(set_micro_step_res, &dsub_pos_stepper_motor_name, "<device> <resolution>",
		      cmd_stepper_set_micro_step_res, 3, 0),
	SHELL_CMD_ARG(set_actual_position, &dsub_pos_stepper_motor_name, "<device> <position>",
		      cmd_stepper_set_actual_position, 3, 0),
	SHELL_CMD_ARG(set_target_position, &dsub_pos_stepper_motor_name, "<device> <micro_steps>",
		      cmd_stepper_set_target_position, 3, 0),
	SHELL_CMD_ARG(enable_constant_velocity_mode, &dsub_pos_stepper_motor_name,
		      "<device> <direction> <velocity>", cmd_stepper_enable_constant_velocity_mode,
		      4, 0),
	SHELL_CMD_ARG(info, &dsub_pos_stepper_motor_name, "<device>", cmd_stepper_info, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(stepper, &stepper_cmds, "Stepper motor commands", NULL);
