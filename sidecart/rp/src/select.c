#include "select.h"

static reset_callback_t reset_cb = NULL;
static reset_callback_t reset_long_cb = NULL;
static volatile bool core1WaitActive = false;
static bool selectPressedLatched = false;
static absolute_time_t selectPressStartTime;
static bool selectLongPressDetected = false;

// Stronger debouncer ported from md-drives-emulator: poll the button
// every SELECT_LOOP_DELAY ms and require SELECT_DEBOUNCE_MS of
// continuously matching samples before accepting the state. Replaces a
// 2-sample debouncer that could be fooled by a single bouncing edge
// during the 20 ms window.
static bool select_detectStableState(bool expectedState) {
  uint32_t stable_ms = 0;
  while (stable_ms < SELECT_DEBOUNCE_MS) {
    if (select_detectPush() != expectedState) {
      return false;
    }
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
    stable_ms += SELECT_LOOP_DELAY;
  }
  return true;
}

static uint32_t select_getPressDurationMs(void) {
  int64_t elapsedUs =
      absolute_time_diff_us(selectPressStartTime, get_absolute_time());
  if (elapsedUs <= 0) {
    return 0;
  }

  return (uint32_t)(elapsedUs / 1000);
}

void __not_in_flash_func(select_waitPush)() {
  DPRINTF("Waiting for SELECT button release\n");

  if (!select_detectStableState(true)) {
    DPRINTF("SELECT button was not stably pressed\n");
    return;
  }

  uint32_t press_duration = 0;
  bool longPressDetected = false;
  while (select_detectPush()) {
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
    if (press_duration < SELECT_LONG_RESET) {
      press_duration += SELECT_LOOP_DELAY;
      if (press_duration >= SELECT_LONG_RESET) {
        longPressDetected = true;
      }
    }
  }

  while (!select_detectStableState(false)) {
    tight_loop_contents();
    sleep_ms(SELECT_LOOP_DELAY);
  }

  DPRINTF("SELECT button released after %lu ms\n", (unsigned long)press_duration);
  if (longPressDetected) {
    if (reset_long_cb != NULL) {
      DPRINTF("Long press detected. Executing long reset callback\n");
      reset_long_cb();
    }
  } else {
    if (reset_cb != NULL) {
      DPRINTF("Short press detected. Executing reset callback\n");
      reset_cb();
    }
  }
  DPRINTF("SELECT button callback returned!\n");
}

void select_configure() {
  // Configure the input ping for SELECT button
  gpio_init(SELECT_GPIO);
  gpio_set_dir(SELECT_GPIO, GPIO_IN);
  gpio_set_pulls(SELECT_GPIO, false, true);  // Pull down (false, true)
  gpio_pull_down(SELECT_GPIO);
}

bool select_detectPush() { return (gpio_get(SELECT_GPIO) != 0); }

void select_coreWaitPush(reset_callback_t reset, reset_callback_t resetLong) {
  inline void core1_waitPush(void) {
    DPRINTF("Waiting for SELECT button to be pushed\n");
    while (core1WaitActive && !select_detectStableState(true)) {
      tight_loop_contents();
      sleep_ms(SELECT_LOOP_DELAY);
    }

    if (!core1WaitActive) {
      return;
    }

    DPRINTF("SELECT button pushed!\n");
    select_waitPush();
    core1WaitActive = false;
  }

  reset_cb = reset;
  reset_long_cb = resetLong;

  if (core1WaitActive) {
    DPRINTF("Core 1 wait for SELECT is already active\n");
    return;
  }

  DPRINTF("Launching core 1 to wait for SELECT button push\n");
  core1WaitActive = true;
  multicore_launch_core1(core1_waitPush);
}

void select_coreWaitPushDisable() {
  if (!core1WaitActive) {
    DPRINTF("Core 1 wait for SELECT is already disabled\n");
    return;
  }

  DPRINTF("Disabling core 1\n");
  core1WaitActive = false;
  multicore_reset_core1();
}

void select_checkPushReset() {
  bool isPressed = select_detectPush();
  if (isPressed && !selectPressedLatched) {
    if (!select_detectStableState(true)) {
      return;
    }

    selectPressedLatched = true;
    selectPressStartTime = get_absolute_time();
    selectLongPressDetected = false;
    DPRINTF("SELECT button pushed. Waiting for release\n");
    return;
  }

  if (isPressed && selectPressedLatched) {
    if (!selectLongPressDetected &&
        (select_getPressDurationMs() >= SELECT_LONG_RESET)) {
      selectLongPressDetected = true;
      DPRINTF("SELECT button long press threshold reached\n");
    }
    return;
  }

  if (!isPressed && selectPressedLatched) {
    if (!select_detectStableState(false)) {
      return;
    }

    uint32_t pressDurationMs = select_getPressDurationMs();
    bool longPress = selectLongPressDetected ||
                     (pressDurationMs >= SELECT_LONG_RESET);
    selectPressedLatched = false;
    selectLongPressDetected = false;

    DPRINTF("SELECT button released after %lu ms\n",
            (unsigned long)pressDurationMs);
    if (longPress) {
      if (reset_long_cb != NULL) {
        DPRINTF("Long press detected. Executing long reset callback\n");
        reset_long_cb();
      }
    } else {
      if (reset_cb != NULL) {
        DPRINTF("Short press detected. Executing reset callback\n");
        reset_cb();
      }
    }
  }
}

void select_setResetCallback(reset_callback_t reset) { reset_cb = reset; }
void select_setLongResetCallback(reset_callback_t resetLong) {
  reset_long_cb = resetLong;
}
